#include "trace.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "board.h"
#include "task.h"

enum { EV_SWITCH, EV_ISR_ENTER, EV_ISR_EXIT };

struct event {
    uint32_t ts;
    void *who;
    uint8_t type;
    uint8_t isr;
};

static struct event g_ring[TRACE_LEN];
static uint32_t g_head;
static volatile bool g_frozen;

/* Runs inside the kernel with interrupts masked up to the syscall level,
 * and from ISRs: keep it short and lock-free. */
static void record(uint8_t type, void *who, uint8_t isr)
{
    if (g_frozen) {
        return;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    struct event *e = &g_ring[g_head++ % TRACE_LEN];
    e->ts = DWT->CYCCNT;
    e->who = who;
    e->type = type;
    e->isr = isr;
    __set_PRIMASK(primask);
}

void trace_switched_in(void *tcb) { record(EV_SWITCH, tcb, 0); }
void trace_isr_enter(uint8_t id) { record(EV_ISR_ENTER, NULL, id); }
void trace_isr_exit(uint8_t id) { record(EV_ISR_EXIT, NULL, id); }
void trace_freeze(void) { g_frozen = true; }

void trace_dump(void)
{
    g_frozen = true;
    uint32_t n = g_head < TRACE_LEN ? g_head : TRACE_LEN;
    uint32_t first = g_head - n;
    uint32_t t0 = g_ring[first % TRACE_LEN].ts;
    uint32_t mhz = SystemCoreClock / 1000000u;

    printf("\n--- trace: last %lu events, time in us from the first ---\n", (unsigned long)n);
    for (uint32_t i = first; i < g_head; i++) {
        const struct event *e = &g_ring[i % TRACE_LEN];
        uint32_t us = (e->ts - t0) / mhz;
        switch (e->type) {
        case EV_SWITCH:
            printf("%8lu  run %s\n", (unsigned long)us, pcTaskGetName((TaskHandle_t)e->who));
            break;
        case EV_ISR_ENTER:
            printf("%8lu    isr %u >\n", (unsigned long)us, e->isr);
            break;
        default:
            printf("%8lu    isr %u <\n", (unsigned long)us, e->isr);
            break;
        }
    }
    printf("--- end of trace ---\n");
}
