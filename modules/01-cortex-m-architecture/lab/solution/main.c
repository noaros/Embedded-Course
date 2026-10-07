/*
 * Lab 1: Map the machine.
 *
 * Times a 4 KB copy inside DTCM, AXI SRAM and SRAM4 with the caches off and
 * on, then tries DMA1 into SRAM1 and into DTCM. Reference solution.
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

#define BARRIER() do { __DSB(); __asm volatile("" ::: "memory"); } while (0)

static void measure_overhead(void)
{
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < 8; i++) {
        BARRIER();
        uint32_t t0 = DWT->CYCCNT;
        BARRIER();
        uint32_t t1 = DWT->CYCCNT;
        BARRIER();
        if (t1 - t0 < best) {
            best = t1 - t0;
        }
    }
    g_overhead = best;
}

static uint32_t time_copy(copy_fn fn, uint8_t *dst, const uint8_t *src)
{
    BARRIER();
    uint32_t t0 = DWT->CYCCNT;
    BARRIER();
    fn(dst, src, g_copy_size);
    BARRIER();
    uint32_t t1 = DWT->CYCCNT;
    BARRIER();
    uint32_t cycles = t1 - t0;
    return cycles > g_overhead ? cycles - g_overhead : 0;
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

        static const copy_fn fns[] = {copy_memcpy, copy_words};
        for (size_t f = 0; f < 2; f++) {
            if (caches) {
                /* Empty the D-cache so the cold run really starts cold. The
                 * I-cache stays warm: we measure data, not code, placement. */
                SCB_CleanInvalidateDCache();
            }
            uint32_t cold = time_copy(fns[f], r->dst, r->src);
            uint32_t warm = time_copy(fns[f], r->dst, r->src);
            print_cpb(cold);
            print_cpb(warm);
            printf("   ");
        }
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

    /* The pattern may still be sitting in dirty D-cache lines: write it to RAM. */
    SCB_CleanDCache_by_Addr(axi_src, BUF_SIZE);

    /* a) AXI SRAM -> SRAM1: both on DMA1's side of the bus matrix. */
    memset(sram1_dst, 0, BUF_SIZE);
    SCB_CleanDCache_by_Addr(sram1_dst, BUF_SIZE);
    struct dma_result r = dma1_m2m_copy(sram1_dst, axi_src, BUF_SIZE);
    /* Drop any stale cached copy of the destination before reading it. */
    SCB_InvalidateDCache_by_Addr(sram1_dst, BUF_SIZE);
    print_dma("AXI -> SRAM1", r, memcmp(sram1_dst, axi_src, BUF_SIZE) == 0);

    /* b) AXI SRAM -> DTCM: DTCM hangs off the core, DMA1 has no path to it.
     * Expect TE=1, TC=0 and the destination untouched. DTCM is never cached,
     * so no maintenance is needed on this side. */
    memset(dtcm_dst, 0, BUF_SIZE);
    r = dma1_m2m_copy(dtcm_dst, axi_src, BUF_SIZE);
    print_dma("AXI -> DTCM", r, memcmp(dtcm_dst, axi_src, BUF_SIZE) == 0);
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
