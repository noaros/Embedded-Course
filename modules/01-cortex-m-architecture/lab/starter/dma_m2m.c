#include "dma_m2m.h"

#include "board.h"

/* Stream 0 flag bits in DMA_LISR / DMA_LIFCR. */
#define S0_FEIF  (1u << 0)
#define S0_DMEIF (1u << 2)
#define S0_TEIF  (1u << 3)
#define S0_HTIF  (1u << 4)
#define S0_TCIF  (1u << 5)
#define S0_ALL   (S0_FEIF | S0_DMEIF | S0_TEIF | S0_HTIF | S0_TCIF)

struct dma_result dma1_m2m_copy(void *dst, const void *src, size_t bytes)
{
    DMA_Stream_TypeDef *s = DMA1_Stream0;

    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    s->CR &= ~DMA_SxCR_EN;
    while (s->CR & DMA_SxCR_EN) {
    }
    DMA1->LIFCR = S0_ALL;

    /* Memory-to-memory needs no DMAMUX request: the stream runs as soon as it
     * is enabled. In this mode PAR is the SOURCE and M0AR the destination. */
    DMAMUX1_Channel0->CCR = 0;
    s->PAR = (uint32_t)src;
    s->M0AR = (uint32_t)dst;
    s->NDTR = (uint32_t)(bytes / 4u);
    /* Direct mode is not allowed for memory-to-memory: enable the FIFO, full threshold. */
    s->FCR = DMA_SxFCR_DMDIS | DMA_SxFCR_FTH_0 | DMA_SxFCR_FTH_1;
    s->CR = DMA_SxCR_DIR_1                       /* 10: memory-to-memory */
          | DMA_SxCR_PINC | DMA_SxCR_MINC
          | DMA_SxCR_PSIZE_1 | DMA_SxCR_MSIZE_1; /* 32-bit both sides */
    s->CR |= DMA_SxCR_EN;

    uint32_t start = dwt_cycles();
    uint32_t timeout = SystemCoreClock / 100u;
    while ((s->CR & DMA_SxCR_EN) && (dwt_cycles() - start) < timeout) {
    }

    uint32_t flags = DMA1->LISR;
    struct dma_result r = {
        .complete = (flags & S0_TCIF) != 0,
        .transfer_error = (flags & S0_TEIF) != 0,
        .fifo_error = (flags & S0_FEIF) != 0,
        .remaining = s->NDTR,
    };
    s->CR &= ~DMA_SxCR_EN;
    DMA1->LIFCR = S0_ALL;
    return r;
}
