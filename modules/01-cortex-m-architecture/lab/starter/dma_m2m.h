#ifndef DMA_M2M_H
#define DMA_M2M_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct dma_result {
    bool complete;          /* TCIF: transfer complete */
    bool transfer_error;    /* TEIF: a bus error on the source or destination */
    bool fifo_error;        /* FEIF */
    uint32_t remaining;     /* NDTR when the stream stopped, in 32-bit items */
};

/*
 * Copies `bytes` (a multiple of 4) from src to dst using DMA1 stream 0 in
 * memory-to-memory mode, polling until the stream stops or ~10 ms pass.
 * Does no cache maintenance: the caller handles coherency.
 */
struct dma_result dma1_m2m_copy(void *dst, const void *src, size_t bytes);

#endif
