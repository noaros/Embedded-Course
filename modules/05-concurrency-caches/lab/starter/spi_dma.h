/*
 * SPI1 full-duplex transfers via DMA1 streams 4 (RX) and 5 (TX).
 * Pins: PA5 SCK, PA6 MISO, PA7 MOSI (AF5). For the lab, jumper MOSI to MISO.
 */
#ifndef SPI_DMA_H
#define SPI_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPI_FRAME_LEN 100u

struct spi_stats {
    uint32_t started;   /* transfers started (main loop) */
    uint32_t tx_done;   /* TX DMA complete interrupts */
    uint32_t rx_done;   /* RX DMA complete interrupts */
};

void spi_dma_init(void);

/* Sends tx[0..SPI_FRAME_LEN) and receives the same number of bytes.
 * Returns false on timeout. On success, *rx points at the received frame. */
bool spi_dma_transfer(const uint8_t *tx, const uint8_t **rx);

void spi_dma_get_stats(struct spi_stats *out);

/* Provided by the application; called while waiting for a transfer. */
void app_idle_hook(void);

#endif
