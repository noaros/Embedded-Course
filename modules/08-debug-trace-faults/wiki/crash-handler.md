# Writing a crash handler

[← Module 8 wiki](README.md) · Lecture notes: [§8.4](../README.md#84-a-hardfault-handler-that-tells-you-something) · Lab: [steps 1–3](../lab/README.md#steps)

The goal: a crash in the field leaves behind enough evidence to find the faulting line, with no debugger attached. The design has two halves.

```
  crash                                 next boot
  ─────                                 ─────────
  fault vector                          startup (doesn't touch .noinit)
    → naked entry: pick MSP/PSP           → main(): crash_report_if_any()
    → C: validate SP, copy frame,             magic OK?  CRC OK?
         fault regs, stack snapshot            → print / upload / store in flash
    → magic + CRC                              → clear the record
    → NVIC_SystemReset()
```

**Capture** does as little as possible, in a broken system. **Reporting** does everything else, later, in a healthy one.

## The naked entry

```c
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4          \n"   /* EXC_RETURN bit 2: which stack? */
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"   /* pass EXC_RETURN as the 2nd argument */
        "b     fault_handler_c \n"); /* B, not BL: we never come back */
}

void MemManage_Handler(void)  __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void)   __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
```

Why it's **naked**: a normal C function starts with a prologue (`push {r4-r7, lr}`, `sub sp, #n`). That would move MSP away from the frame before you've captured it, and if MSP is the broken stack, the push itself faults and causes [lockup](fault-exceptions.md#lockup). A naked function has no prologue, so the first instruction that runs is yours.

The core puts EXC_RETURN in LR. The entry reads which stack held the frame from it ([details](exception-frame.md#which-stack-msp-or-psp)) and passes both values to C in R0 and R1, following the normal calling convention.

## Robustness rules

The handler runs in a system that has just proved itself broken. Assume anything may be corrupt.

1. **Validate the frame pointer before dereferencing it.** If the fault was a `STKERR` (SP outside RAM), reading `frame[6]` raises a fault inside HardFault, which locks up the core. Check that the whole frame and snapshot range lie inside real RAM:

   ```c
   static bool in_ram(uint32_t a, uint32_t len)   /* H723: DTCM, AXI, SRAM1-2, SRAM4 */
   {
       return (a >= 0x20000000u && a + len <= 0x20020000u) ||
              (a >= 0x24000000u && a + len <= 0x24050000u) ||
              (a >= 0x30000000u && a + len <= 0x30008000u) ||
              (a >= 0x38000000u && a + len <= 0x38004000u);
   }
   ```

   If the check fails, record a sentinel such as `0xBADBADBA` and the raw SP value. That's itself a strong clue.

2. **Don't depend on the faulting stack.** If the fault was a stack overflow on MSP, the C part runs on that same nearly exhausted MSP. Keep `fault_handler_c` small and leaf-like. A robust option is to switch to a small dedicated stack in the naked entry (`ldr sp, =_fault_stack_top`, after saving the old value into R0). On ARMv8-M, MSPLIM catches overflows before they corrupt anything.

3. **No `printf`, no RTOS calls, no `malloc`, no HAL.** They may hold locks, use the broken stack, or depend on the state that's just been corrupted. Copy registers, compute a CRC, reset.

4. **Make the record self-validating.** Write `magic = 0` first, fill in the record, then write the magic and finally the CRC. A crash *during* capture leaves a record that won't validate, not a half-written one that looks real.

5. **Clear the CFSR** (write the value back, since it's write-1-to-clear) so the next crash isn't polluted with old bits.

6. **`__DSB()` before `NVIC_SystemReset()`**, so the record's stores have actually reached RAM. (CMSIS's `NVIC_SystemReset()` includes DSBs, but being explicit costs nothing.)

7. **Have a watchdog anyway.** If the handler itself locks up, only the watchdog brings the system back. There's no record in that case, but the reset-cause register shows a watchdog reset.

## The crash record

Lab 8's record (`lab/starter/crash.h`):

```c
struct crash_record {
    uint32_t magic;                 /* CRASH_MAGIC when complete */
    uint32_t boot_count_at_crash;
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;   /* the stacked frame */
    uint32_t exc_return;
    uint32_t sp;                    /* where the frame was */
    uint32_t ipsr;                  /* which handler ran */
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t stack[CRASH_STACK_WORDS];            /* words above the frame */
    uint32_t crc;                   /* CRC-32 of everything before it */
};
```

What each field is for:

| Field | Why |
| --- | --- |
| `magic` + `crc` | separate a real record from random RAM contents after power-on, or from a half-written record |
| frame | PC and LR for `addr2line`; R0–R3 often hold the bad pointer |
| `exc_return`, `sp` | which stack, FP or not; is SP plausible? |
| `ipsr` | which handler ran (once the [configurable faults are enabled](fault-exceptions.md#enable-them)) |
| `cfsr`, `hfsr`, `mmfar`, `bfar` | [what happened and where](fault-registers.md) |
| `stack[]` | candidate return addresses for a [backtrace](post-mortem.md#backtraces) |

### What a product record adds

- **Build ID or firmware version.** Without it, you can't pick the right ELF ([why](post-mortem.md#keep-the-exact-elf)).
- **Reset cause** (on the H7, `RCC->RSR`: watchdog, brown-out, pin, software). A watchdog reset leaves no fault record, so this is how you know one happened.
- **Uptime or timestamp, and the current RTOS task name or ID.**
- **A small log tail**, for example the last N [deferred-log](logging.md#deferred-binary-logging) entries from a RAM ring.
- **A crash count.** A boot loop shows up as a crash count that keeps rising. The bootloader can use it to roll back ([Module 10](../../10-bootloaders-updates/README.md)).
- A **watchdog early-warning interrupt** (for example the WWDG early-wakeup interrupt) that captures a record *before* the watchdog reset. That turns hangs into crash reports with a PC.
- An `assert()` handler that writes the same record type, with the file/line or the return address of the failing assert.

## Where the record lives

The record must survive the reset, so put it in memory that startup **neither zeroes nor initialises**:

| Location | Survives | Notes |
| --- | --- | --- |
| `NOLOAD` RAM section (`NOINIT` → `.noinit` in DTCM; `SRAM4_NOINIT` → `.sram4_noinit`) | warm resets (software, watchdog, pin) | Lab 8 uses SRAM4; there's no CPU cost and no wear |
| RTC backup registers / backup SRAM | resets, and power loss if VBAT is present | small (tens of words to a few KB); needs the backup domain unlocked |
| Flash | everything | write it on the **next boot**, not in the fault handler (erasing takes milliseconds to seconds, and the flash driver may itself be broken) |

The linker side, from `bsp/h723/h723_sections.ld`:

```ld
/* SRAM4, not touched by startup: survives any reset except power loss. */
.sram4_noinit (NOLOAD) :
{
    . = ALIGN(8);
    *(.sram4_noinit .sram4_noinit.*)
    . = ALIGN(8);
} > SRAM4
```

```c
SRAM4_NOINIT struct crash_record g_crash;   /* board.h: __attribute__((section(".sram4_noinit"))) */
```

After power-on, the section contains random data, which is exactly why the record needs a magic number **and** a CRC. A 32-bit magic alone matches random data about once in 4 billion boots, but also matches any record that was half-written when the crash happened.

## The reporting side

```c
void crash_report_if_any(void)
{
    if (g_crash.magic != CRASH_MAGIC) return;                 /* nothing to report */
    if (crash_crc32(&g_crash, offsetof(struct crash_record, crc)) != g_crash.crc) {
        /* corrupted: say so and discard */
    }
    /* decode and print: handler, PC/LR, CFSR bit names, MMFAR/BFAR if valid, stack */
    memset(&g_crash, 0, sizeof g_crash);                      /* report once */
}
```

Call it early in `main()`, after the console is up. In a product, also copy the record to flash or queue it for upload *before* clearing it.

## Testing the handler

Trigger each fault class on purpose and check that you get a record:

```c
*(volatile uint32_t *)0xC0000000u = 1;     /* bus error (imprecise) */
((void (*)(void))0)();                      /* INVSTATE, PC = 0 */
__asm volatile(".short 0xDE00");            /* UNDEFINSTR (permanently undefined encoding) */
```

To test the hardest case, a bad SP: point MSP at an address with nothing behind it, such as `__set_MSP(0x60000100)` (the FMC bank, with the FMC disabled), then call any function that pushes. Exception entry then fails to stack (`STKERR`), and the handler receives a frame pointer outside RAM. Without rule 1, that locks up the core.
