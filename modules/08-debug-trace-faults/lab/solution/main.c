/*
 * Lab 8: Crash detective. Five firmware images, one per CRASH_CASE, each
 * crashing in a different way a few seconds after boot. Your crash.c
 * captures the fault, the board resets, and the next boot prints the report.
 *
 * You must not change this file to diagnose a case: work from the report.
 */
#include <stdio.h>

#include "board.h"
#include "crash.h"

#ifndef CRASH_CASE
#define CRASH_CASE 1
#endif

NOINIT static uint32_t g_boot_count;

/* ---- case 1 ------------------------------------------------------- */

/* A callback table entry nobody registered. volatile: so the compiler
 * can't see that it's NULL and replace the call with a trap. */
static void (*volatile g_on_packet)(const uint8_t *data, uint32_t len);

static void case1_dispatch(void)
{
    static const uint8_t pkt[4] = {1, 2, 3, 4};
    g_on_packet(pkt, sizeof pkt);
}

/* ---- case 2 ------------------------------------------------------- */

static void default_handler(void)
{
    gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
}

/* Initialised data: lives at the bottom of DTCM, below .bss, the heap and
 * the stack (see bsp/h723/h723.ld). */
static void (*g_event_handler)(void) = default_handler;

/* A "recursive descent parser" for nested input. Each level keeps a 256-byte
 * scratch buffer. The input here is maliciously deep: the recursion only
 * stops once the stack has grown down over g_event_handler. (The stop
 * condition just makes the demo deterministic; a real bug simply has no
 * depth limit.) */
__attribute__((noinline)) static uint32_t case2_parse(uint32_t depth)
{
    volatile uint32_t scratch[64];
    for (unsigned i = 0; i < 64; i++) {
        scratch[i] = 0xA5A5A5A5u;
    }
    if ((uintptr_t)&scratch[0] <= (uintptr_t)&g_event_handler) {
        return scratch[0];
    }
    return case2_parse(depth + 1) + scratch[depth & 63u];
}

static void case2_overflow(void)
{
    (void)case2_parse(0);
    g_event_handler();
}

/* ---- case 3 ------------------------------------------------------- */

/* "Log to external SDRAM", but nobody enabled the FMC or its clock. */
#define EXT_SDRAM ((volatile uint32_t *)0xC0000000u)

__attribute__((noinline)) static void case3_log_to_sdram(uint32_t value)
{
    EXT_SDRAM[0] = value;
    EXT_SDRAM[1] = value ^ 0xFFFFFFFFu;
}

/* ---- case 4 ------------------------------------------------------- */

/* A 64-bit field read from a packed protocol buffer at an odd offset. LDRD
 * needs word alignment even when unaligned single loads are allowed. */
__attribute__((noinline)) static uint64_t case4_read_u64(const uint8_t *p)
{
    uint32_t lo, hi;
    __asm volatile("ldrd %0, %1, [%2]" : "=r"(lo), "=r"(hi) : "r"(p) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

/* ---- case 5 ------------------------------------------------------- */

static volatile int32_t g_samples_in_window; /* 0 if the sensor stalled */

__attribute__((noinline)) static int32_t case5_average(int32_t sum)
{
    return sum / g_samples_in_window;
}

/* ------------------------------------------------------------------- */

int main(void)
{
    board_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    g_boot_count++;
    crash_init();
    printf("\nLab 8, image %d, boot %lu\n", CRASH_CASE, (unsigned long)g_boot_count);
    crash_report_if_any();

    printf("Running normally for 3 s...\n");
    board_delay_ms(3000);

    switch (CRASH_CASE) {
    case 1:
        case1_dispatch();
        break;
    case 2:
        case2_overflow();
        break;
    case 3:
        case3_log_to_sdram(0x1234u);
        printf("logged to SDRAM\n");
        break;
    case 4: {
        static const uint8_t packet[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        printf("timestamp %llu\n", (unsigned long long)case4_read_u64(&packet[1]));
        break;
    }
    case 5:
        SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk; /* the product enables this "for safety" */
        printf("average %ld\n", (long)case5_average(1000));
        break;
    default:
        break;
    }

    printf("survived?\n");
    for (;;) {
    }
}
