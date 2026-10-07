/*
 * DWT cycle counter helpers. The counter is 32 bits wide, so at 400 MHz it
 * wraps every ~10.7 s; unsigned subtraction (end - start) stays correct
 * across one wrap.
 */
#ifndef COURSE_DWT_H
#define COURSE_DWT_H

#include <stdint.h>

static inline void dwt_init(void)
{
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
#if defined(__CORE_CM7_H_GENERIC)
    /* The Cortex-M7 DWT has a CoreSight lock; writes are ignored until it is unlocked. */
    DWT->LAR = 0xC5ACCE55u;
#endif
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

static inline void dwt_delay_cycles(uint32_t cycles)
{
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
    }
}

#endif /* COURSE_DWT_H */
