/*
 * Lab 8 reference solution: fault capture.
 *
 * All four fault vectors share one naked entry. It picks the stack the
 * faulting code was on and passes the frame to C, which copies everything
 * into a CRC-protected record in SRAM4 (not zeroed by startup) and resets.
 * The next boot prints it (crash_report.c).
 */
#include "crash.h"

#include <stddef.h>

#include "board.h"

SRAM4_NOINIT struct crash_record g_crash;
NOINIT static uint32_t g_boot_count_shadow;

/* RAM the frame may legally be in: DTCM, AXI SRAM, SRAM1-4. */
static bool in_ram(uint32_t a, uint32_t len)
{
    return (a >= 0x20000000u && a + len <= 0x20020000u) ||
           (a >= 0x24000000u && a + len <= 0x24050000u) ||
           (a >= 0x30000000u && a + len <= 0x30008000u) ||
           (a >= 0x38000000u && a + len <= 0x38004000u);
}

void crash_init(void)
{
    /* Separate handlers for each class: HFSR.FORCED no longer hides the
     * real cause, and each can run at its own priority. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk;
    g_boot_count_shadow++;
}

__attribute__((used, noreturn))
void fault_handler_c(const uint32_t *frame, uint32_t exc_return)
{
    struct crash_record *r = &g_crash;

    r->magic = 0; /* invalid until complete */
    r->boot_count_at_crash = g_boot_count_shadow;
    r->exc_return = exc_return;
    r->sp = (uint32_t)frame;
    r->ipsr = __get_IPSR();
    r->cfsr = SCB->CFSR;
    r->hfsr = SCB->HFSR;
    r->mmfar = SCB->MMFAR;
    r->bfar = SCB->BFAR;

    /* Only dereference the frame if it is really in RAM: reading a bad
     * stack pointer here would fault again and lock up the core. */
    if (in_ram((uint32_t)frame, (8u + CRASH_STACK_WORDS) * 4u)) {
        r->r0 = frame[0];
        r->r1 = frame[1];
        r->r2 = frame[2];
        r->r3 = frame[3];
        r->r12 = frame[4];
        r->lr = frame[5];
        r->pc = frame[6];
        r->xpsr = frame[7];
        /* The basic frame is 8 words; with an FP frame (EXC_RETURN bit 4 = 0)
         * the caller's stack starts 26 words up. */
        const uint32_t *above = frame + ((exc_return & 0x10u) ? 8u : 26u);
        for (unsigned i = 0; i < CRASH_STACK_WORDS; i++) {
            r->stack[i] = in_ram((uint32_t)&above[i], 4) ? above[i] : 0xBADBADBAu;
        }
    } else {
        r->r0 = r->r1 = r->r2 = r->r3 = r->r12 = r->lr = r->pc = r->xpsr = 0xBADBADBAu;
    }

    SCB->CFSR = SCB->CFSR; /* write-1-to-clear for the next run */
    r->magic = CRASH_MAGIC;
    r->crc = crash_crc32(r, offsetof(struct crash_record, crc));

    __DSB();
    NVIC_SystemReset();
}

/* EXC_RETURN bit 2: 0 = the frame is on MSP, 1 = on PSP. Naked, so no
 * prologue pushes anything onto a stack that may be broken. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "   tst   lr, #4            \n"
        "   ite   eq                \n"
        "   mrseq r0, msp           \n"
        "   mrsne r0, psp           \n"
        "   mov   r1, lr            \n"
        "   b     fault_handler_c   \n");
}

void MemManage_Handler(void) __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void) __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
