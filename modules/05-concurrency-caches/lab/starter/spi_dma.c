/*
 * SPI1 + DMA driver. It passes a quick test, but with the D-cache on it
 * corrupts frames. There are three bugs. Find them, explain each one, and fix
 * them. Don't rewrite the driver from scratch.
 */
#include "spi_dma.h"

#include <string.h>

#include "board.h"

#define REQ_SPI1_RX 37u
#define REQ_SPI1_TX 38u

/* Driver state: DMA buffers and bookkeeping kept together. */
static struct {
    uint8_t tx[SPI_FRAME_LEN];
    uint8_t rx[SPI_FRAME_LEN];
    struct spi_stats stats;
    bool done;
} g_spi AXI_BSS;

void spi_dma_init(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

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
    memcpy(g_spi.tx, tx, SPI_FRAME_LEN);
    SCB_CleanDCache_by_Addr(g_spi.tx, SPI_FRAME_LEN);
    SCB_InvalidateDCache_by_Addr(g_spi.rx, SPI_FRAME_LEN);

    g_spi.done = false;
    g_spi.stats.started++;

    /* RM0468 order: RXDMAEN, streams, TXDMAEN, SPE, CSTART. */
    SPI1->CR2 = SPI_FRAME_LEN << SPI_CR2_TSIZE_Pos;
    SPI1->CFG1 |= SPI_CFG1_RXDMAEN;
    stream_setup(DMA1_Stream4, (uint32_t)&SPI1->RXDR, g_spi.rx, 0);
    stream_setup(DMA1_Stream5, (uint32_t)&SPI1->TXDR, g_spi.tx, DMA_SxCR_DIR_0);
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
        if ((g_spi.stats.started & 0x1FFFu) == 0 && g_spi.rx[0] == 0xFF) {
            gpio_toggle(LED_YELLOW_PORT, LED_YELLOW_PIN);
        }
        if (dwt_cycles() - t0 > SystemCoreClock / 100u) {
            spi_finish();
            return false;
        }
    }
    spi_finish();

    *rx = g_spi.rx;
    return true;
}

void spi_dma_get_stats(struct spi_stats *out)
{
    *out = g_spi.stats;
}
