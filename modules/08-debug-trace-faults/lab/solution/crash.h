/*
 * Crash capture and next-boot reporting (Lab 8).
 */
#ifndef CRASH_H
#define CRASH_H

#include <stdint.h>

#define CRASH_MAGIC 0xC4A54ED1u
#define CRASH_STACK_WORDS 16u

struct crash_record {
    uint32_t magic;
    uint32_t boot_count_at_crash;
    /* Stacked exception frame. */
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
    /* Fault context. */
    uint32_t exc_return;
    uint32_t sp;            /* the stack the frame was on */
    uint32_t ipsr;          /* which fault handler ran */
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t stack[CRASH_STACK_WORDS]; /* words above the frame */
    uint32_t crc;           /* CRC-32 of everything before it */
};

/* Enables the configurable fault handlers so faults aren't all HardFaults. */
void crash_init(void);

/* If the previous run left a valid record, prints it and clears it. */
void crash_report_if_any(void);

uint32_t crash_crc32(const void *data, uint32_t len);

#endif
