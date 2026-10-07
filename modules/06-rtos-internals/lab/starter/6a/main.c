/*
 * Lab 6a test application for the mini kernel.
 *
 *   prio 5  report   every second: context-switch stats, inversion stats, stack use
 *   prio 4  hi       every 200 ms: takes the shared lock, records how long it waited
 *   prio 3  mid      CPU hog: 100 ms busy, 50 ms asleep, never touches the lock
 *   prio 2  ping/pong  yield to each other 1000 times, measuring each switch, then rest
 *   prio 1  lo       holds the shared lock for 20 ms at a time
 *   prio 1  blink    green LED at 1 Hz
 *   prio 0  idle     (created by the kernel)
 *
 * With priority inheritance, hi waits at most ~20 ms (one lo critical section).
 * Without it, mid can keep lo off the CPU and hi waits ~100 ms or more.
 */
#include <stdio.h>

#include "board.h"
#include "kernel.h"

#define TICK_HZ 1000u
#define STACK_WORDS 512u

static uint32_t stk_report[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_hi[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_mid[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_ping[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_pong[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_lo[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_blink[STACK_WORDS] __attribute__((aligned(8)));

static struct mutex g_shared_lock;
static struct tcb *g_task_list[7];

/* Context-switch measurement, written only by ping/pong. */
static volatile uint32_t g_stamp;
static volatile bool g_stamp_valid;
static uint32_t g_sw_min = UINT32_MAX, g_sw_max, g_sw_count;
static uint64_t g_sw_sum;

/* Inversion measurement, written only by hi. */
static uint32_t g_hi_wait_max_ms, g_hi_rounds;

static void busy_ms(uint32_t ms)
{
    board_delay_ms(ms); /* DWT busy-wait: burns CPU, ignores the tick */
}

static void ping_pong(void *arg)
{
    (void)arg;
    for (;;) {
        for (int i = 0; i < 1000; i++) {
            uint32_t now = DWT->CYCCNT;
            if (g_stamp_valid) {
                uint32_t d = now - g_stamp;
                if (d < g_sw_min) {
                    g_sw_min = d;
                }
                if (d > g_sw_max) {
                    g_sw_max = d;
                }
                g_sw_sum += d;
                g_sw_count++;
            }
            g_stamp = DWT->CYCCNT;
            g_stamp_valid = true;
            task_yield();
        }
        g_stamp_valid = false;
        task_sleep(100);
    }
}

static void lo_task(void *arg)
{
    (void)arg;
    for (;;) {
        mutex_lock(&g_shared_lock);
        busy_ms(20);
        mutex_unlock(&g_shared_lock);
        task_sleep(7);
    }
}

static void mid_task(void *arg)
{
    (void)arg;
    for (;;) {
        task_sleep(50);
        busy_ms(100);
    }
}

static void hi_task(void *arg)
{
    (void)arg;
    for (;;) {
        task_sleep(200);
        uint32_t t0 = kernel_ticks();
        mutex_lock(&g_shared_lock);
        uint32_t waited = kernel_ticks() - t0;
        mutex_unlock(&g_shared_lock);
        if (waited > g_hi_wait_max_ms) {
            g_hi_wait_max_ms = waited;
        }
        g_hi_rounds++;
    }
}

static void blink_task(void *arg)
{
    (void)arg;
    for (;;) {
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        task_sleep(500);
    }
}

static void report_task(void *arg)
{
    (void)arg;
    uint32_t mhz = SystemCoreClock / 1000000u;
    for (;;) {
        task_sleep(1000);
        if (g_sw_count) {
            uint32_t avg = (uint32_t)(g_sw_sum / g_sw_count);
            printf("switch: n=%lu min %lu avg %lu max %lu cycles (avg %lu ns)\n",
                   (unsigned long)g_sw_count, (unsigned long)g_sw_min, (unsigned long)avg,
                   (unsigned long)g_sw_max, (unsigned long)(avg * 1000u / mhz));
        }
        printf("hi: %lu rounds, worst wait for lock %lu ms\n", (unsigned long)g_hi_rounds,
               (unsigned long)g_hi_wait_max_ms);
        printf("stack unused:");
        for (unsigned i = 0; i < sizeof g_task_list / sizeof g_task_list[0]; i++) {
            printf(" %s=%lu", g_task_list[i]->name,
                   (unsigned long)task_stack_unused(g_task_list[i]));
        }
        printf("\n\n");
        g_sw_min = UINT32_MAX;
        g_sw_max = g_sw_count = 0;
        g_sw_sum = 0;
        g_hi_wait_max_ms = g_hi_rounds = 0;
    }
}

int main(void)
{
    board_init();
    setvbuf(stdout, NULL, _IONBF, 0); /* no malloc'd stdio buffer */
    printf("\nLab 6a: mini kernel on %s at %lu MHz\n", BOARD_NAME,
           (unsigned long)(SystemCoreClock / 1000000u));

    mutex_init(&g_shared_lock);
    g_task_list[0] = task_create("report", report_task, NULL, stk_report, STACK_WORDS, 5);
    g_task_list[1] = task_create("hi", hi_task, NULL, stk_hi, STACK_WORDS, 4);
    g_task_list[2] = task_create("mid", mid_task, NULL, stk_mid, STACK_WORDS, 3);
    g_task_list[3] = task_create("ping", ping_pong, NULL, stk_ping, STACK_WORDS, 2);
    g_task_list[4] = task_create("pong", ping_pong, NULL, stk_pong, STACK_WORDS, 2);
    g_task_list[5] = task_create("lo", lo_task, NULL, stk_lo, STACK_WORDS, 1);
    g_task_list[6] = task_create("blink", blink_task, NULL, stk_blink, STACK_WORDS, 1);

    kernel_start(TICK_HZ);
}
