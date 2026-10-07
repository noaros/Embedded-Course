/*
 * Lab 8: fault capture. Complete TODO 1-3.
 *
 * Goal: every fault ends up in fault_handler_c() with a pointer to the
 * stacked frame. It copies everything into a CRC-protected record in SRAM4
 * (not zeroed by startup) and resets. The next boot prints it
 * (crash_report.c, complete).
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
    /* TODO 3 (after cases 1-5 work with HardFault only): enable the
     * MemManage, BusFault and UsageFault handlers in SCB->SHCSR, and compare
     * the reports. */
    g_boot_count_shadow++;
}

__attribute__((used, noreturn))
void fault_handler_c(const uint32_t *frame, uint32_t exc_return)
{
    struct crash_record *r = &g_crash;

    /* TODO 2: fill in *r:
     *   - fault context: exc_return, sp (= frame), IPSR, CFSR, HFSR, MMFAR, BFAR
     *   - the 8-word stacked frame (R0-R3, R12, LR, PC, xPSR), but only if
     *     the frame is really in RAM (use in_ram()): a bad SP would fault again
     *   - CRASH_STACK_WORDS words of the caller's stack above the frame
     *     (where does it start if EXC_RETURN says there's an FP frame?)
     *   - clear CFSR (write-1-to-clear), set magic, then the CRC over
     *     everything before the crc field (crash_crc32)
     * Then reset. */
    (void)frame;
    (void)exc_return;
    (void)in_ram;
    r->magic = 0;
    NVIC_SystemReset();
}

/* TODO 1: HardFault_Handler, naked. Use EXC_RETURN (in LR) bit 2 to pick
 * MSP or PSP into r0, pass LR in r1, and branch to fault_handler_c.
 * Then make MemManage_Handler, BusFault_Handler and UsageFault_Handler
 * aliases of it. */
