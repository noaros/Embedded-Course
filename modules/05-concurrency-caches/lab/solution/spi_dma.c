/*
 * SPI1 + DMA driver. Reference solution: the three bugs fixed.
 *
 * Bug 1 (layout): rx shared a 32-byte cache line with tx and with the
 *   counters the TX-complete ISR updates during the transfer. That dirty line
 *   held a stale copy of rx[96..99] (tail corruption), and invalidating rx
 *   threw away counter updates. Fix: DMA buffers in their own 32-byte-aligned,
 *   32-byte-padded storage; bookkeeping elsewhere.
 *
 * Bug 2 (maintenance order): rx was only invalidated BEFORE the transfer. The
 *   debug-trace read of rx[0] during the transfer (and, in general, any
 *   speculative read by the M7) pulled a stale line into the cache. Fix:
 *   invalidate again AFTER completion, before the CPU reads rx.
 *
 * Bug 3 (missing volatile): `done` is written by an ISR and polled in a loop.
 *   Without volatile, once the optimiser can see that nothing in the loop
 *   writes it (-flto inlines app_idle_hook()), it reads the flag once and the
 *   loop only ends by timeout. Fix: volatile (or a C11 atomic).
 *
 * Build with -DSPI_USE_MPU_NOCACHE=1 for the alternative: the buffers live in
 * SRAM1, which the MPU marks Normal non-cacheable, so no maintenance is needed.
 */
#include "spi_dma.h"

#include <string.h>

#include "board.h"

#define REQ_SPI1_RX 37u
#define REQ_SPI1_TX 38u

#define CACHE_LINE 32u
#define DMA_BUF_SIZE ((SPI_FRAME_LEN + CACHE_LINE - 1u) / CACHE_LINE * CACHE_LINE) /* 128 */

#if SPI_USE_MPU_NOCACHE
#define DMA_SECTION SRAM1_BSS /* MPU region 0: whole of SRAM1, non-cacheable */
#else
#define DMA_SECTION AXI_BSS
#endif

/* DMA buffers: each starts on a line boundary and owns whole lines. */
DMA_SECTION static uint8_t g_tx_buf[DMA_BUF_SIZE] __attribute__((aligned(CACHE_LINE)));
DMA_SECTION static uint8_t g_rx_buf[DMA_BUF_SIZE] __attribute__((aligned(CACHE_LINE)));

/* Bookkeeping: ordinary cached memory, never shares a line with a DMA buffer. */
static struct {
    struct spi_stats stats;
    volatile bool done;
} g_spi;

#if SPI_USE_MPU_NOCACHE
static void mpu_init(void)
{
    /* Region 0: SRAM1, 16 KB at 0x30000000, Normal non-cacheable (TEX=001,
     * C=0, B=0), shareable, read/write, execute-never. PRIVDEFENA keeps the
     * default map everywhere else. */
    ARM_MPU_Disable();
    ARM_MPU_SetRegion(ARM_MPU_RBAR(0, 0x30000000u),
                      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 1, 0, 0, 0, ARM_MPU_REGION_SIZE_16KB));
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}
#endif

void spi_dma_init(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

#if SPI_USE_MPU_NOCACHE
    mpu_init();
#endif

    for (unsigned pin = 5; pin <= 7; pin++) {
        gpio_set_af(GPIOA, pin, 5);
        gpio_set_speed(GPIOA, pin, GPIO_HIGH);
    }

    /* SPI1 kernel clock = pll1_q_ck = 100 MHz; /8 -> 12.5 MHz SCK. 8-bit frames.
     * Master, software NSS held high internally, keep pin control while disabled. */
    SPI1->CR1 = SPI_CR1_SSI;
    SPI1->CFG1 = (2u << SPI_CFG1_MBR_Pos) | (7u << SPI_CFG1_DSIZE_Pos);
    SPI1->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR;

    DMAMUX1_Channel4->CCR = REQ_SPI1_RX;
    DMAMUX1_Channel5->CCR = REQ_SPI1_TX;

    NVIC_SetPriority(DMA1_Stream4_IRQn, 4);
    NVIC_SetPriority(DMA1_Stream5_IRQn, 4);
    NVIC_EnableIRQ(DMA1_Stream4_IRQn);
    NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

static void stream_setup(DMA_Stream_TypeDef *s, uint32_t periph, void *mem, uint32_t dir)
{
    s->CR = 0;
    while (s->CR & DMA_SxCR_EN) {
    }
    s->PAR = periph;
    s->M0AR = (uint32_t)mem;
    s->NDTR = SPI_FRAME_LEN;
    s->FCR = 0;
    s->CR = dir | DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_PL_1;
}

/* RX complete: the whole frame has arrived. */
void DMA1_Stream4_IRQHandler(void)
{
    DMA1->HIFCR = DMA_HIFCR_CTCIF4 | DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4 | DMA_HIFCR_CFEIF4 |
                  DMA_HIFCR_CDMEIF4;
    g_spi.stats.rx_done++;
    g_spi.done = true;
}

/* TX complete: the last byte has gone into the SPI FIFO; RX is still running. */
void DMA1_Stream5_IRQHandler(void)
{
    DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CFEIF5 |
                  DMA_HIFCR_CDMEIF5;
    g_spi.stats.tx_done++;
}

static void spi_finish(void)
{
    uint32_t t0 = dwt_cycles();
    while (!(SPI1->SR & SPI_SR_EOT) && (dwt_cycles() - t0) < 100000u) {
    }
    SPI1->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_OVRC | SPI_IFCR_MODFC;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CFG1 &= ~(SPI_CFG1_RXDMAEN | SPI_CFG1_TXDMAEN);
}

bool spi_dma_transfer(const uint8_t *tx, const uint8_t **rx)
{
    memcpy(g_tx_buf, tx, SPI_FRAME_LEN);
#if !SPI_USE_MPU_NOCACHE
    /* TX: write our data out to RAM for the DMA. RX: make sure no dirty line
     * can be evicted on top of what the DMA writes. */
    SCB_CleanDCache_by_Addr(g_tx_buf, DMA_BUF_SIZE);
    SCB_InvalidateDCache_by_Addr(g_rx_buf, DMA_BUF_SIZE);
#endif

    g_spi.done = false;
    g_spi.stats.started++;

    /* RM0468 order: RXDMAEN, streams, TXDMAEN, SPE, CSTART. */
    SPI1->CR2 = SPI_FRAME_LEN << SPI_CR2_TSIZE_Pos;
    SPI1->CFG1 |= SPI_CFG1_RXDMAEN;
    stream_setup(DMA1_Stream4, (uint32_t)&SPI1->RXDR, g_rx_buf, 0);
    stream_setup(DMA1_Stream5, (uint32_t)&SPI1->TXDR, g_tx_buf, DMA_SxCR_DIR_0);
    DMA1_Stream4->CR |= DMA_SxCR_EN;
    DMA1_Stream5->CR |= DMA_SxCR_EN;
    SPI1->CFG1 |= SPI_CFG1_TXDMAEN;
    SPI1->CR1 |= SPI_CR1_SPE;
    SPI1->CR1 |= SPI_CR1_CSTART;

    uint32_t t0 = dwt_cycles();
    while (!g_spi.done) {
        app_idle_hook();
        /* Debug trace: every 8192 frames, show the first received byte so we
         * can see the transfer making progress on a scope trigger. */
        if ((g_spi.stats.started & 0x1FFFu) == 0 && g_rx_buf[0] == 0xFF) {
            gpio_toggle(LED_YELLOW_PORT, LED_YELLOW_PIN);
        }
        if (dwt_cycles() - t0 > SystemCoreClock / 100u) {
            spi_finish();
            return false;
        }
    }
    spi_finish();

#if !SPI_USE_MPU_NOCACHE
    /* Discard anything (speculatively or by the trace read above) cached
     * from rx while the DMA was writing it. */
    SCB_InvalidateDCache_by_Addr(g_rx_buf, DMA_BUF_SIZE);
#endif
    *rx = g_rx_buf;
    return true;
}

void spi_dma_get_stats(struct spi_stats *out)
{
    *out = g_spi.stats;
}
