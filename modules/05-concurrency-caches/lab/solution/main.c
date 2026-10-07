/*
 * Lab 5: Break it, then fix it. Test harness (same as the starter).
 *
 * Jumper PA7 (MOSI, Arduino D11) to PA6 (MISO, Arduino D12). The harness
 * sends numbered frames with a checksum through SPI loopback and checks every
 * received frame, plus the driver's own counters. It prints a report every
 * 10 000 frames.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "spi_dma.h"

#define REPORT_EVERY 10000u

static uint32_t g_idle_calls;

void app_idle_hook(void)
{
    g_idle_calls++;
}

static void make_frame(uint8_t *f, uint32_t seq)
{
    f[0] = (uint8_t)seq;
    f[1] = (uint8_t)(seq >> 8);
    f[2] = (uint8_t)(seq >> 16);
    f[3] = (uint8_t)(seq >> 24);
    uint8_t x = (uint8_t)(seq * 31u + 7u);
    for (size_t i = 4; i < SPI_FRAME_LEN; i++) {
        x = (uint8_t)(x * 13u + 1u);
        f[i] = x;
    }
}

int main(void)
{
    board_init();
    spi_dma_init();
    printf("\nLab 5: SPI DMA loopback soak test (D-cache %s)\n",
           (SCB->CCR & SCB_CCR_DC_Msk) ? "ON" : "off");

    static uint8_t tx[SPI_FRAME_LEN];
    uint32_t frames = 0, bad = 0, bad_head = 0, bad_tail = 0, timeouts = 0;
    uint32_t window_start = dwt_cycles();
    uint64_t busy = 0;

    for (uint32_t seq = 1;; seq++) {
        make_frame(tx, seq);
        const uint8_t *rx = NULL;

        uint32_t t0 = dwt_cycles();
        bool ok = spi_dma_transfer(tx, &rx);
        busy += dwt_cycles() - t0;

        frames++;
        if (!ok) {
            timeouts++;
        } else if (memcmp(rx, tx, SPI_FRAME_LEN) != 0) {
            bad++;
            if (memcmp(rx, tx, 32) != 0) {
                bad_head++;
            }
            if (memcmp(rx + 96, tx + 96, SPI_FRAME_LEN - 96) != 0) {
                bad_tail++;
            }
        }

        if (frames == REPORT_EVERY) {
            struct spi_stats st;
            spi_dma_get_stats(&st);
            uint32_t elapsed = dwt_cycles() - window_start;
            uint32_t kbps = (uint32_t)((uint64_t)REPORT_EVERY * SPI_FRAME_LEN * SystemCoreClock /
                                       elapsed / 1000u);
            printf("%lu frames: bad %lu (head %lu, tail %lu), timeouts %lu | "
                   "started %lu tx_done %lu rx_done %lu | %lu kB/s, %lu%% in transfer\n",
                   (unsigned long)frames, (unsigned long)bad, (unsigned long)bad_head,
                   (unsigned long)bad_tail, (unsigned long)timeouts, (unsigned long)st.started,
                   (unsigned long)st.tx_done, (unsigned long)st.rx_done, (unsigned long)kbps,
                   (unsigned long)(busy * 100u / elapsed));
            frames = bad = bad_head = bad_tail = timeouts = 0;
            busy = 0;
            window_start = dwt_cycles();
        }
    }
}
