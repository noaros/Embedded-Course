/*
 * Lab 1: Map the machine.
 *
 * Times a 4 KB copy inside DTCM, AXI SRAM and SRAM4 with the caches off and
 * on, then tries DMA1 into SRAM1 and into DTCM. Complete the TODOs.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "dma_m2m.h"

#define BUF_SIZE 4096u

/* .bss is placed in DTCM by the course linker script. */
static uint8_t dtcm_src[BUF_SIZE] __attribute__((aligned(32)));
static uint8_t dtcm_dst[BUF_SIZE] __attribute__((aligned(32)));
AXI_BSS static uint8_t axi_src[BUF_SIZE] __attribute__((aligned(32)));
AXI_BSS static uint8_t axi_dst[BUF_SIZE] __attribute__((aligned(32)));
SRAM4_BSS static uint8_t sram4_src[BUF_SIZE] __attribute__((aligned(32)));
SRAM4_BSS static uint8_t sram4_dst[BUF_SIZE] __attribute__((aligned(32)));
SRAM1_BSS static uint8_t sram1_dst[BUF_SIZE] __attribute__((aligned(32)));

struct region {
    const char *name;
    uint8_t *src;
    uint8_t *dst;
};

static const struct region regions[] = {
    {"DTCM ", dtcm_src, dtcm_dst},
    {"AXI  ", axi_src, axi_dst},
    {"SRAM4", sram4_src, sram4_dst},
};

typedef void (*copy_fn)(void *dst, const void *src, size_t n);

/* Passing the size through a volatile stops the compiler inlining memcpy. */
static volatile size_t g_copy_size = BUF_SIZE;

static uint32_t g_overhead;

static void copy_memcpy(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

/* A plain 32-bit word loop, for comparison with newlib-nano's memcpy. */
__attribute__((noinline))
static void copy_words(void *dst, const void *src, size_t n)
{
    uint32_t *d = dst;
    const uint32_t *s = src;
    for (size_t i = 0; i < n / 4u; i++) {
        d[i] = s[i];
    }
}

static void measure_overhead(void)
{
    /* TODO 2a: time an empty measurement (two CYCCNT reads with the same
     * barriers time_copy() uses) and store it in g_overhead. */
    g_overhead = 0;
}

static uint32_t time_copy(copy_fn fn, uint8_t *dst, const uint8_t *src)
{
    /* TODO 2b: read DWT->CYCCNT, call fn(dst, src, g_copy_size), read it
     * again, and return the difference minus g_overhead. Put __DSB() and a
     * compiler barrier on both sides so nothing moves across the reads. */
    fn(dst, src, g_copy_size);
    return 0;
}

/* Prints cycles per byte as a fixed-point number with two decimals. */
static void print_cpb(uint32_t cycles)
{
    uint32_t x100 = (cycles * 100u + BUF_SIZE / 2) / BUF_SIZE;
    printf(" %3lu.%02lu", (unsigned long)(x100 / 100u), (unsigned long)(x100 % 100u));
}

static void measure_all(bool caches)
{
    board_caches(caches, caches);
    printf("\nCaches %s           memcpy cold/warm   words cold/warm\n", caches ? "ON " : "OFF");

    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
        const struct region *r = &regions[i];
        printf("  %s (%p)      ", r->name, (void *)r->src);

        /* TODO 3: for memcpy, then copy_words:
         *   - if caches are on, SCB_CleanInvalidateDCache() before the cold run
         *   - time a cold run, then a warm run
         *   - print both with print_cpb() */
        (void)time_copy;
        (void)copy_memcpy;
        (void)copy_words;
        (void)print_cpb;
        printf("\n");
    }
}

static void print_dma(const char *what, struct dma_result r, bool data_ok)
{
    printf("  %-18s TC=%d TE=%d FE=%d NDTR left=%lu data %s\n", what, r.complete,
           r.transfer_error, r.fifo_error, (unsigned long)r.remaining, data_ok ? "OK" : "WRONG");
}

static void dma_experiment(void)
{
    printf("\nDMA1 memory-to-memory, %u bytes\n", (unsigned)BUF_SIZE);

    for (size_t i = 0; i < BUF_SIZE; i++) {
        axi_src[i] = (uint8_t)(i * 7u + 3u);
    }

    /* TODO 4: make the CPU's writes to axi_src visible to the DMA, then
     *   a) copy axi_src -> sram1_dst, check with memcmp, print_dma()
     *   b) copy axi_src -> dtcm_dst,  check with memcmp, print_dma()
     * Think about which cache maintenance the destination needs before the
     * CPU reads it back with memcmp. */
    (void)print_dma;
    (void)sram1_dst;
}

int main(void)
{
    board_init();
    printf("\nLab 1: map the machine (%s, %lu MHz)\n", BOARD_NAME,
           (unsigned long)(SystemCoreClock / 1000000u));

    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
        for (size_t j = 0; j < BUF_SIZE; j++) {
            regions[i].src[j] = (uint8_t)j;
        }
    }

    measure_overhead();
    printf("CYCCNT read overhead: %lu cycles\n", (unsigned long)g_overhead);

    measure_all(false);
    measure_all(true);
    dma_experiment();

    for (;;) {
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        board_delay_ms(500);
    }
}
