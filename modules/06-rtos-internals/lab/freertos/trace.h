/*
 * Minimal context-switch tracer for FreeRTOS (Lab 6b).
 *
 * Every switch is recorded as (DWT timestamp, task) in a ring buffer.
 * ISRs can mark their entry and exit too. trace_dump() prints the last
 * TRACE_LEN events as a timeline, so you can see who ran when without a
 * J-Link (SystemView) or a Tracealyzer licence.
 */
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>

#define TRACE_LEN 256u

void trace_switched_in(void *tcb);   /* called by the kernel (FreeRTOSConfig.h) */
void trace_isr_enter(uint8_t id);
void trace_isr_exit(uint8_t id);
void trace_freeze(void);             /* stop recording (e.g. on a deadline miss) */
void trace_dump(void);               /* print the recorded timeline */

#endif
