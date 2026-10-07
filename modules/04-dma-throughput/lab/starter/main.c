/*
 * Lab 4: 2 MSPS data logger. Starter: complete TODO 1-5.
 *
 *   TIM6 TRGO (2 MHz) ─► ADC1 (PA3, 16-bit) ─► DMA1 S1 circular ─► adc_buf[2 x 1024] (SRAM1)
 *        HT/TC interrupt ─► main loop: FIR /16 ─► frame ─► TX queue ─► DMA1 S2 ─► USART3 @ 3 Mbaud
 *   USART3 RX ─► DMA1 S3 circular ring (SRAM2) ─► IDLE interrupt ─► command line
 *
 * Commands (newline-terminated): start, stop, stats
 * Host side: tools/lab4_rx.py
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "fir.h"
#include "frame.h"

#define SAMPLE_RATE_HZ 2000000u
#define HALF_SAMPLES 1024u
#define OUT_PER_HALF (HALF_SAMPLES / FIR_DECIM) /* 64 */
#define UART_BAUD 3000000u

#define REQ_ADC1 9u
#define REQ_USART3_RX 45u
#define REQ_USART3_TX 46u
/* ADC1/2 external trigger 13 = tim6_trgo. Check RM0468, "ADC1/2 external triggers". */
#define ADC_EXTSEL_TIM6_TRGO 13u
#define ADC_CHANNEL 15u /* PA3 = ADC12_INP15 (Arduino A0) */

/*
 * Link budget: one 136-byte sample frame per 512 us = 266 kB/s, against
 * ~303 kB/s for 3.03 Mbaud 8N1. The 12% slack absorbs the once-a-second
 * status text, provided the TX side can queue a few frames.
 */
#define FRAME_MAX (FRAME_HDR_SIZE + OUT_PER_HALF * 2u + FRAME_CRC_SIZE) /* 136 */
#define FRAME_BUF_SIZE 160u /* padded to whole 32-byte cache lines */
#define TX_QUEUE_LEN 4u     /* power of two */
#define RX_RING_SIZE 64u

/* DMA buffers live in D2 SRAM, away from the CPU's DTCM data, and are
 * aligned and sized to whole cache lines (Module 5 explains why). */
SRAM1_BSS static uint16_t adc_buf[2 * HALF_SAMPLES] __attribute__((aligned(32)));
SRAM2_BSS static uint8_t tx_buf[TX_QUEUE_LEN][FRAME_BUF_SIZE] __attribute__((aligned(32)));
SRAM2_BSS static uint8_t rx_ring[RX_RING_SIZE] __attribute__((aligned(32)));

static struct fir_decim g_fir;
static int16_t g_out[OUT_PER_HALF];

/* Shared between ISRs and the main loop. */
static volatile uint32_t g_half_events;   /* HT/TC count, written by the DMA ISR only */
static volatile uint32_t g_adc_overruns;  /* ADC OVR (DMA too slow) */
static volatile uint32_t g_dma_errors;
static volatile bool g_cmd_ready;
/* TX queue: the main loop produces (head), the DMA TC interrupt consumes (tail). */
static uint16_t g_tx_len[TX_QUEUE_LEN];
static volatile uint32_t g_tx_head, g_tx_tail;
static volatile bool g_tx_active;
static char g_cmd_line[32];

/* Main-loop only. */
static uint32_t g_processed;
static uint32_t g_missed_halves;
static uint16_t g_seq;
static uint32_t g_tx_drops;
static uint32_t g_busy_cycles;
static bool g_running;

/* ------------------------------------------------------------------ */
/* Clocks                                                              */
/* ------------------------------------------------------------------ */

/* PLL2: 8 MHz / 4 = 2 MHz, x200 = 400 MHz VCO, P /8 = 50 MHz ADC kernel clock. */
static void pll2_init(void)
{
    RCC->PLLCKSELR = (RCC->PLLCKSELR & ~RCC_PLLCKSELR_DIVM2) | (4u << RCC_PLLCKSELR_DIVM2_Pos);
    RCC->PLLCFGR = (RCC->PLLCFGR & ~(RCC_PLLCFGR_PLL2RGE | RCC_PLLCFGR_PLL2VCOSEL |
                                     RCC_PLLCFGR_PLL2FRACEN)) |
                   (1u << RCC_PLLCFGR_PLL2RGE_Pos) | RCC_PLLCFGR_DIVP2EN;
    RCC->PLL2DIVR = ((200u - 1) << RCC_PLL2DIVR_N2_Pos) | ((8u - 1) << RCC_PLL2DIVR_P2_Pos) |
                    ((2u - 1) << RCC_PLL2DIVR_Q2_Pos) | ((2u - 1) << RCC_PLL2DIVR_R2_Pos);
    RCC->CR |= RCC_CR_PLL2ON;
    while (!(RCC->CR & RCC_CR_PLL2RDY)) {
    }
    /* ADCSEL = 00 (pll2_p_ck) is the reset value. */
    RCC->D3CCIPR &= ~RCC_D3CCIPR_ADCSEL;
}

/* ------------------------------------------------------------------ */
/* ADC + TIM6 + DMA1 stream 1                                          */
/* ------------------------------------------------------------------ */

static void adc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_ADC12EN | RCC_AHB1ENR_DMA1EN;
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    gpio_set_mode(GPIOA, 3, GPIO_ANALOG);

    /* TODO 1: ADC1 bring-up (RM0468 "ADC on-off control", "Calibration"):
     *   - ADC12_COMMON->CCR: asynchronous clock (CKMODE = 0), PRESC = 0
     *   - leave deep power-down, enable the regulator, wait for LDORDY
     *   - BOOST for a 25-50 MHz ADC clock
     *   - single-ended linearity calibration (ADCALLIN, ADCAL), wait
     *   - enable (ADEN), wait for ADRDY
     *   - channel ADC_CHANNEL: preselection (PCSEL, named PCSEL_RES0 in the H72x
 *     header), SQR1, 2.5-cycle sampling in SMPR2
     *   - CFGR: DMNGT = circular DMA, EXTEN rising, EXTSEL = ADC_EXTSEL_TIM6_TRGO
     *   - IER: overrun interrupt */

    /* TODO 2: TIM6 at SAMPLE_RATE_HZ with TRGO on update (CR2.MMS = 010). */

    /* TODO 3: DMA1 stream 1, request REQ_ADC1 via DMAMUX1_Channel1:
     *   ADC1->DR (16-bit) -> adc_buf, 2 * HALF_SAMPLES items, circular,
     *   memory increment, HT + TC + TE interrupts, very high priority. */

    NVIC_SetPriority(DMA1_Stream1_IRQn, 2);
    NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    NVIC_SetPriority(ADC_IRQn, 2);
    NVIC_EnableIRQ(ADC_IRQn);

    /* Finally: ADC1->CR |= ADC_CR_ADSTART; (conversions wait for the trigger) */
}

void DMA1_Stream1_IRQHandler(void)
{
    /* TODO 4: clear HT/TC/TE flags for stream 1 (LIFCR) and add the number
     * of HT/TC events seen to g_half_events. Count TE in g_dma_errors. */
}

/* ADC overrun: the DMA did not read DR before the next conversion finished.
 * Recover without a reset (Module 4 §4.7). */
void ADC_IRQHandler(void)
{
    if (ADC1->ISR & ADC_ISR_OVR) {
        g_adc_overruns++;
        ADC1->ISR = ADC_ISR_OVR;
        ADC1->CR |= ADC_CR_ADSTART;
    }
}

/* ------------------------------------------------------------------ */
/* USART3 TX (DMA1 stream 2) and RX (DMA1 stream 3, idle line)         */
/* ------------------------------------------------------------------ */

static void uart_dma_init(void)
{
    board_uart_init(UART_BAUD); /* GPIO, clocks, BRR; we take over from here */
    USART3->CR1 &= ~USART_CR1_UE;
    USART3->CR3 |= USART_CR3_DMAT | USART_CR3_DMAR;
    USART3->CR1 |= USART_CR1_IDLEIE | USART_CR1_UE;

    /* TX stream: configured per frame in tx_start(). */
    DMAMUX1_Channel2->CCR = REQ_USART3_TX;
    NVIC_SetPriority(DMA1_Stream2_IRQn, 5);
    NVIC_EnableIRQ(DMA1_Stream2_IRQn);

    /* TODO 5b: DMA1 stream 3, request REQ_USART3_RX via DMAMUX1_Channel3:
     * USART3->RDR -> rx_ring, RX_RING_SIZE bytes, circular, memory increment.
     * No DMA interrupts: the USART IDLE interrupt drives reception. */

    NVIC_SetPriority(USART3_IRQn, 6);
    NVIC_EnableIRQ(USART3_IRQn);
}

static void tx_start(uint32_t slot)
{
    /* TODO 5a: program DMA1 stream 2 (memory -> USART3->TDR, 8-bit,
     * memory increment, TC + TE interrupts) to send tx_buf[slot] with
     * g_tx_len[slot] bytes, enable it, and set g_tx_active. */
    (void)slot;
}

void DMA1_Stream2_IRQHandler(void)
{
    uint32_t isr = DMA1->LISR;
    DMA1->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CTEIF2;
    if (isr & DMA_LISR_TEIF2) {
        g_dma_errors++;
    }
    uint32_t tail = g_tx_tail + 1u;
    g_tx_tail = tail;
    if (g_tx_head != tail) {
        tx_start(tail % TX_QUEUE_LEN);
    } else {
        g_tx_active = false;
    }
}

/* Builds a frame in the next free queue slot and starts the DMA if idle.
 * Returns false (frame dropped) when the queue is full. */
static bool tx_enqueue(uint8_t type, uint16_t seq, const void *payload, uint16_t len)
{
    uint32_t head = g_tx_head;
    if (head - g_tx_tail >= TX_QUEUE_LEN) {
        return false;
    }
    uint32_t slot = head % TX_QUEUE_LEN;
    g_tx_len[slot] = (uint16_t)frame_build(tx_buf[slot], type, seq, payload, len);
    /* The CPU wrote the frame through the D-cache: push it out to SRAM2
     * before the DMA reads it. */
    SCB_CleanDCache_by_Addr(tx_buf[slot], FRAME_BUF_SIZE);
    __DMB();
    g_tx_head = head + 1u;

    /* Kick the DMA if it is idle. Masking the TC interrupt makes the
     * check-and-start atomic with respect to the ISR. */
    NVIC_DisableIRQ(DMA1_Stream2_IRQn);
    if (!g_tx_active) {
        tx_start(g_tx_tail % TX_QUEUE_LEN);
    }
    NVIC_EnableIRQ(DMA1_Stream2_IRQn);
    return true;
}

/* Copies newly received bytes [from, to) of the ring into the command line. */
__attribute__((unused)) static void rx_consume(uint32_t from, uint32_t to)
{
    static size_t len;
    SCB_InvalidateDCache_by_Addr(rx_ring, RX_RING_SIZE);
    while (from != to) {
        char c = (char)rx_ring[from];
        from = (from + 1u) % RX_RING_SIZE;
        if (c == '\r' || c == '\n') {
            if (len > 0 && !g_cmd_ready) {
                g_cmd_line[len] = '\0';
                g_cmd_ready = true;
            }
            len = 0;
        } else if (len < sizeof g_cmd_line - 1) {
            g_cmd_line[len++] = c;
        }
    }
}

void USART3_IRQHandler(void)
{
    /* TODO 5c: on IDLE, clear the flag, compute the DMA write position from
     * DMA1_Stream3->NDTR, pass [last position, new position) to
     * rx_consume(), and remember the new position. Clear ORE if set. */
    if (USART3->ISR & USART_ISR_IDLE) {
        USART3->ICR = USART_ICR_IDLECF;
    }
    if (USART3->ISR & USART_ISR_ORE) {
        USART3->ICR = USART_ICR_ORECF;
    }
}

/* ------------------------------------------------------------------ */
/* Pipeline                                                            */
/* ------------------------------------------------------------------ */

static void start_sampling(void)
{
    fir_decim_init(&g_fir);
    g_processed = g_half_events;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->CR1 = TIM_CR1_CEN;
    g_running = true;
}

static void stop_sampling(void)
{
    TIM6->CR1 = 0;
    g_running = false;
}

static void process_half(unsigned half)
{
    const uint16_t *src = &adc_buf[half * HALF_SAMPLES];

    /* The DMA wrote RAM behind the cache's back: drop any stale lines. */
    SCB_InvalidateDCache_by_Addr((void *)src, HALF_SAMPLES * sizeof adc_buf[0]);
    fir_decim_process(&g_fir, src, HALF_SAMPLES, g_out);

    if (!tx_enqueue(FRAME_SAMPLES, g_seq, g_out, sizeof g_out)) {
        g_tx_drops++; /* seq still advances: the host sees the gap */
    }
    g_seq++;
}

static void send_text(const char *text)
{
    size_t len = strlen(text);
    if (len > FRAME_BUF_SIZE - FRAME_HDR_SIZE - FRAME_CRC_SIZE) {
        len = FRAME_BUF_SIZE - FRAME_HDR_SIZE - FRAME_CRC_SIZE;
    }
    tx_enqueue(FRAME_TEXT, 0, text, (uint16_t)len);
}

static void send_stats(void)
{
    char line[120];
    uint32_t load_x10 = (uint32_t)(((uint64_t)g_busy_cycles * 1000u) / SystemCoreClock);
    snprintf(line, sizeof line,
             "%s load=%lu.%lu%% seq=%u missed=%lu adc_ovr=%lu tx_drop=%lu dma_err=%lu",
             g_running ? "RUN" : "STOP", (unsigned long)(load_x10 / 10), (unsigned long)(load_x10 % 10),
             (unsigned)g_seq, (unsigned long)g_missed_halves, (unsigned long)g_adc_overruns,
             (unsigned long)g_tx_drops, (unsigned long)g_dma_errors);
    send_text(line);
}

static void handle_command(const char *cmd)
{
    if (strcmp(cmd, "start") == 0) {
        start_sampling();
    } else if (strcmp(cmd, "stop") == 0) {
        stop_sampling();
    } else if (strcmp(cmd, "stats") == 0) {
        send_stats();
    } else {
        send_text("ERR unknown command");
    }
}

int main(void)
{
    board_init();
    printf("\nLab 4: 2 MSPS logger. Switching to %lu baud framed output.\n",
           (unsigned long)UART_BAUD);
    board_delay_ms(20);

    pll2_init();
    uart_dma_init();
    adc_init();
    send_text("READY");

    uint32_t last_stats = dwt_cycles();

    for (;;) {
        uint32_t events = g_half_events;
        if (events != g_processed) {
            if (events - g_processed > 1u) {
                /* More than one half completed since we last looked: at
                 * least one was overwritten before we processed it. */
                g_missed_halves += events - g_processed - 1u;
                g_processed = events - 1u;
            }
            uint32_t t0 = dwt_cycles();
            process_half(g_processed & 1u);
            g_busy_cycles += dwt_cycles() - t0;
            g_processed++;
        }

        if (g_cmd_ready) {
            handle_command(g_cmd_line);
            g_cmd_ready = false;
        }

        if (dwt_cycles() - last_stats >= SystemCoreClock) {
            last_stats = dwt_cycles();
            if (g_running) {
                send_stats();
            }
            g_busy_cycles = 0;
        }
    }
}
