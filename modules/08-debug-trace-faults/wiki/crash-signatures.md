# Crash signatures

[← Module 8 wiki](README.md)

> **Doing Lab 8?** Try to diagnose each case from the report and the [fault register tables](fault-registers.md) before you come here. This page is a general lookup table, not a list of the lab's answers, but it will narrow things down a lot.

A lookup table from the pattern in a crash report to the most likely root causes. "Escalated" means you'll see the same CFSR bits under `HardFault, HFSR FORCED` if the [configurable handlers](fault-exceptions.md#enable-them) aren't enabled.

## By CFSR bit

| You see | Most likely | Check |
| --- | --- | --- |
| `INVSTATE`, PC = 0 | call through a **NULL function pointer** (callback never registered, zeroed struct) | `addr2line` the LR to find the caller; who should have set the pointer? |
| `INVSTATE`, PC an even nonzero address | function pointer built without the Thumb bit (a cast from a data address, or a hand-computed address), or a corrupt pointer | how the pointer was formed; did someone write a data value over it? |
| `IACCVIOL` or `IBUSERR`, PC is garbage (`0xA5A5A5A4`, `0xDEADBEEE`, …) | the core **returned or branched to a data value**: a stack overwrite clobbered a saved LR, or a function pointer was overwritten | is the PC value a fill pattern or a constant in your code? Search the stack snapshot and the source for it |
| `IACCVIOL`, PC in RAM or peripheral space | jump into data, or into an MPU execute-never region | a bad function pointer or a corrupt return address |
| `UNDEFINSTR` | execution ran into data or erased flash (`0xFFFF`), or code was corrupted | is the PC past the end of `.text`? Did a flash write hit code? |
| `NOCP` | FPU instruction before `CPACR` enables the FPU | startup order; a library using `float` before `SystemInit` |
| `INVPC` | a corrupt EXC_RETURN: a handler overwrote its own stack, or a hand-written context switch is broken | the RTOS port, naked handlers, stack sizes of ISRs |
| `UNALIGNED` with `UNALIGN_TRP` clear | LDRD/STRD/LDM/STM on an unaligned address: packed structs, casts of `uint8_t *` to `uint64_t *` or `double *` | disassemble at the PC for `ldrd`/`ldm`; use `memcpy` for packed fields |
| `DIVBYZERO` | integer division by a zero denominator with `DIV_0_TRP` set | where the denominator came from; an empty window, a stalled sensor |
| `PRECISERR` + `BFARVALID` | access to a disabled or unclocked peripheral, a reserved address, or a bad pointer | look up BFAR in the memory map; is that peripheral's clock enabled? |
| `IMPRECISERR` (no BFAR) | a **store** to a bad address, a few instructions before the PC | [make it precise](precise-imprecise.md); look at the stores before the PC |
| `DACCVIOL` + `MMARVALID` | MPU-protected access: a stack guard hit, a null-page guard, writing to read-only memory | look up MMFAR: a guard region means overflow; near 0 means a NULL struct pointer + offset |
| `MSTKERR` or `STKERR` | **stack overflow**: SP walked into a guard region or off the end of RAM | stack sizes, recursion, large locals; the SP value in the record |
| `STKOF` (v8-M) | stack overflow caught by MSPLIM/PSPLIM | as above, caught cleanly before any damage |

## By HFSR, when the CFSR is empty

| You see | Most likely |
| --- | --- |
| `VECTTBL` | bad `VTOR` (often after a bootloader jumps to an app), a vector table in unpowered or unmapped memory, or an exception number with no entry |
| `DEBUGEVT` | a leftover `__BKPT()` or `assert` → `BKPT`, a semihosting call (`printf` with `rdimon.specs`), or a DWT/FPB match with no debugger attached |
| `FORCED` + empty CFSR | the CFSR was cleared before you read it (by your handler or a library), or you're reading the registers from the wrong core or context |

## By address pattern

| Value | Meaning |
| --- | --- |
| `0x00000000`–`0x000000FF` (BFAR/MMFAR) | NULL pointer to a struct; the offset is the member |
| `0xA5A5A5A5` / `0xA5A5A5A4` | FreeRTOS's (and many other kernels') stack fill byte: a value read from never-written stack. As a PC, a stack overflow or a frame that was read from the wrong place |
| `0xDEADBEEF`, `0xCCCCCCCC`, `0xCDCDCDCD` | common fill or poison patterns: memory that was freed, never initialised or deliberately poisoned |
| `0xFFFFFFFF` / `0xFFFFFFFE` | erased flash or an uninitialised pointer read from erased flash |
| `0xFFFFFFxx` as PC | an EXC_RETURN value used as an ordinary address (a broken context switch) |
| `0x6xxxxxxx`, `0x7xxxxxxx`, `0x9xxxxxxx`, `0xCxxxxxxx`, `0xDxxxxxxx` on the H723 | FMC/OCTOSPI external memory regions: a fault there usually means the controller or its clock isn't set up |
| `0x4xxxxxxx`, `0x5xxxxxxx` | peripheral space: is that peripheral's bus clock enabled in RCC? |

## By symptom, when there's no record

| Symptom | Suspect |
| --- | --- |
| The board stops responding, there's no report, and the debugger shows `DHCSR.S_LOCKUP` | [lockup](fault-exceptions.md#lockup): the fault handler itself faulted (bad SP, no `in_ram()` check) |
| A watchdog reset with no fault record | a hang, not a crash: an infinite loop, a deadlock, waiting for a flag that never sets. Add a watchdog early-warning capture |
| The report is present but the CRC is wrong | a crash during capture, or a power loss; or the record layout changed between builds |
| Crashes stop when you add a `printf` | timing (a race) or stack layout (overflow, uninitialised locals). Remove the `printf` and use RTT, or watch the variable |
| Crashes only at `-O2` | undefined behaviour (missing `volatile` on shared data, strict aliasing, uninitialised variables), or a timing change |
