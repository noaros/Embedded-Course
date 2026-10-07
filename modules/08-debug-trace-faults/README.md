# Module 8: Debugging, tracing and fault analysis

**Time:** 5 h (2 h theory, 3 h lab) · **Board:** NUCLEO-H723ZG · **Lab:** [Crash detective](lab/README.md)

By the end, you can root-cause a HardFault from a field crash log alone, without a debugger attached. In the field, you'll never have a probe on the board. You get what the firmware saved before it reset, and nothing else.

## Learning objectives

1. Use SWD debugging beyond breakpoints: DWT watchpoints, conditional breakpoints, live variable watch.
2. Stream non-intrusive logs and events via ITM/SWO and SEGGER RTT.
3. Decode CFSR, HFSR, MMFAR and BFAR, and unwind the stacked frame to the faulting PC.
4. Build a crash-dump mechanism that survives reset and reports on the next boot.

---

## 8.1 CoreSight on Cortex-M

| Block | What it gives you | Pins / bandwidth |
| --- | --- | --- |
| **DAP** (SW-DP) | memory access while the core runs: live watch, flashing | SWDIO + SWCLK |
| **FPB** | hardware breakpoints (6–8 on M3/M4/M7, 8 on M33) and, on v7-M, flash patching | — |
| **DWT** | cycle counter, 4 watchpoint comparators (data address/value, PC), PC sampling, exception tracing | — |
| **ITM** | 32 stimulus ports: `printf`-style output and timestamps | over SWO or the TPIU |
| **TPIU / SWO** | serialised trace output | 1 pin (SWO, a few Mbit/s) or 4-bit parallel trace port |
| **ETM** | instruction trace: every branch the core took | parallel trace port (TRACED0-3 + TRACECLK), or to the on-chip ETF buffer |

**Watchpoints** catch "who wrote this variable?" bugs, which breakpoints can't. In GDB: `watch g_state` (write), `rwatch` (read), `awatch` (either). The debugger programs a DWT comparator. A conditional watch like `watch g_state if g_state == 3` halts on every write and checks the condition on the host, which is slow but works. Without a debugger, the **DebugMonitor** exception can take the watchpoint hit in firmware (`DCB->DEMCR.MON_EN`). It's useful for catching a corrupting write in the field.

**Live watch**: Cortex-Debug and STM32CubeIDE can read RAM over SWD while the core runs, with no halt and no instrumentation. It's ideal for counters and state variables, with sampling rates of tens of Hz.

## 8.2 Logging without `printf` costs

A blocking UART `printf` at 115200 baud costs about 87 µs per character. Adding one changes the timing you're trying to debug. Alternatives:

| Method | Intrusiveness | Needs | Notes |
| --- | --- | --- | --- |
| **ITM stimulus port** | a few cycles per word (if the FIFO has room) | SWO pin (PB3 on the H7) + a probe that captures SWO | `ITM_SendChar()` in CMSIS; set SWO speed and the TPIU prescaler to match the probe |
| **SEGGER RTT** | a `memcpy` into a RAM ring buffer | J-Link (or any probe whose host software reads the ring by address: OpenOCD, pyOCD and probe-rs support RTT) | bidirectional; no pins; tens of kB/s+; doesn't block when no host is attached (mode `NO_BLOCK_SKIP`) |
| Semihosting | **halts the core** for every call | debugger | never in timing-sensitive code; hangs without a debugger |
| **Deferred binary logging** | store a format-string ID + raw arguments, format on the host | any byte transport | Trice, defmt (Rust) and similar; 5–10× less bandwidth than text |

Deferred logging in a nutshell:

```c
/* target: no formatting at all, just an ID and raw words */
#define LOG2(id, a, b) log_write3((id), (uint32_t)(a), (uint32_t)(b))
LOG2(LOG_ADC_OVERRUN, stream, ndtr);              /* 12 bytes on the wire */

/* host: a table built from the source at compile time maps
 * LOG_ADC_OVERRUN -> "ADC overrun on stream %u, NDTR=%u" */
```

## 8.3 Anatomy of a fault

All configurable faults escalate to **HardFault** unless enabled in `SCB->SHCSR` (`MEMFAULTENA`, `BUSFAULTENA`, `USGFAULTENA`). Enable them in every product. The individual handlers can then run at a configurable priority, and HFSR's `FORCED` bit stops hiding the real class.

### Fault status registers

**CFSR** (`0xE000ED28`) = UFSR[31:16] | BFSR[15:8] | MMFSR[7:0]:

| Bit | Name | Meaning |
| --- | --- | --- |
| 0 | IACCVIOL | instruction fetch from an MPU-forbidden or execute-never address |
| 1 | DACCVIOL | data access violated the MPU |
| 3 | MUNSTKERR | MPU fault while unstacking on exception return |
| 4 | MSTKERR | MPU fault while stacking on exception entry (typically a stack overflowing into a guard region) |
| 5 | MLSPERR | MPU fault during lazy FP state preservation |
| 7 | **MMARVALID** | MMFAR holds the faulting address |
| 8 | IBUSERR | bus error on instruction fetch |
| 9 | **PRECISERR** | precise data bus error: stacked PC **is** the faulting instruction; BFAR valid |
| 10 | **IMPRECISERR** | imprecise data bus error: stacked PC is **later** than the faulting store; BFAR not valid |
| 11 | UNSTKERR | bus error on unstacking |
| 12 | STKERR | bus error on stacking (stack pointer outside RAM) |
| 13 | LSPERR | bus error during lazy FP preservation |
| 15 | **BFARVALID** | BFAR holds the faulting address |
| 16 | UNDEFINSTR | undefined instruction (corrupt code, jump into data) |
| 17 | **INVSTATE** | tried to execute in ARM state: a branch to an address with bit 0 = 0 (null or corrupt function pointer) |
| 18 | INVPC | invalid EXC_RETURN (stack corruption in a handler) |
| 19 | NOCP | coprocessor (FPU) not enabled |
| 20 | STKOF | stack-limit violation (ARMv8-M only, MSPLIM/PSPLIM) |
| 24 | UNALIGNED | unaligned access with `CCR.UNALIGN_TRP`, or any unaligned LDRD/LDM/STRD/STM |
| 25 | DIVBYZERO | integer divide by zero with `CCR.DIV_0_TRP` |

**HFSR**: `FORCED` (bit 30) means a configurable fault escalated (look in CFSR). `VECTTBL` (bit 1) means a bus error reading the vector table (bad VTOR). `DEBUGEVT` (bit 31) means a breakpoint or watchpoint was hit with no debugger attached, typically a leftover `__BKPT()`.

**MMFAR / BFAR** hold the data address, but only when `MMARVALID` / `BFARVALID` is set. Read CFSR **first**, then the address registers, then clear CFSR (write-1-to-clear), because a later fault overwrites them.

ARMv8-M with TrustZone adds **SFSR**/**SFAR** for SecureFaults (Module 9).

### Precise vs imprecise bus faults

Stores to Normal and Device memory go through the core's **write buffer**: the core continues before the bus answers. If the bus later returns an error, the instruction that caused it has already retired and the core is several instructions further on. The fault is **imprecise**: `IMPRECISERR`, no BFAR, and the stacked PC somewhere after the store.

Techniques to pin it down:

- **On Cortex-M3/M4:** set `SCnSCB->ACTLR.DISDEFWBUF`. This disables the write buffer for the default memory map, so every bus fault becomes precise, at a performance cost. **The Cortex-M7 doesn't have this bit** (check `core_cm7.h`). Its write path is more complex (AXI, store buffer, merging).
- **On Cortex-M7 and others:** bisect with `__DSB()` after suspect stores. The DSB stalls until the store completes, so the fault is raised at the DSB and the stacked PC lands right after it. The fault is still flagged IMPRECISERR, but the location is pinned to one store.
- Map the suspicious region as **Strongly-ordered/Device** with the MPU, and read the address back (`(void)*p`) after writing. The read fault is precise.

## 8.4 A HardFault handler that tells you something

```c
/* Every fault vector lands here. Find the stack the faulting code was using
 * (EXC_RETURN bit 2) and hand the frame to C. Naked: no prologue may touch
 * the possibly broken stack before we've looked at it. */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4          \n"
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "b     fault_handler_c \n");
}

void fault_handler_c(const uint32_t *frame, uint32_t exc_return)
{
    rec->r0 = frame[0];  rec->r1 = frame[1];  rec->r2 = frame[2];  rec->r3 = frame[3];
    rec->r12 = frame[4]; rec->lr = frame[5];  rec->pc = frame[6];  rec->xpsr = frame[7];
    rec->cfsr = SCB->CFSR; rec->hfsr = SCB->HFSR;
    rec->mmfar = SCB->MMFAR; rec->bfar = SCB->BFAR;
    rec->exc_return = exc_return;
    /* ... stack snapshot, CRC, magic ... then reset */
    NVIC_SystemReset();
}
```

The stacked frame is `R0, R1, R2, R3, R12, LR, PC, xPSR` (Module 3 §3.2). **PC** is the faulting instruction (for precise faults), and **LR** is usually the return address of the faulting function, which gives you one level of call stack for free.

Robustness rules for the handler:

- **Validate the frame pointer** before dereferencing it. If SP is outside RAM (a `STKERR`), reading the frame faults again and the core **locks up**. Check the range first and record "SP invalid" instead.
- **Don't use the faulting stack** for the handler's own work. Switch to a small dedicated stack, or keep the C part frame-less.
- **No `printf`, no RTOS calls, no malloc.** Record and reset. Report on the next boot, when the system is healthy.
- Save to memory that **survives reset but isn't zeroed by startup**: a `NOLOAD` section in SRAM (`.sram4_noinit` in the course BSP), the RTC backup registers, or flash for crashes that should survive power loss. Protect the record with a magic number and a CRC.

## 8.5 Post-mortem: from record to source line

```sh
$ arm-none-eabi-addr2line -f -C -e lab08_case3.elf 0x08000a3e 0x08000b19
write_external_ram
/…/lab/solution/main.c:61
main
/…/lab/solution/main.c:148
```

Keep the **exact ELF** of every released build: same source isn't enough, because the build ID or timestamp differs. A build ID in the firmware header (Lab 2's stretch goal), reported in every crash record, identifies the ELF unambiguously.

Going deeper without a debugger:

- **Stack snapshot**: save 16–64 words above the frame. Return addresses in it (values in the `.text` range) give a probabilistic backtrace.
- **Frame-pointer or unwind-table backtraces**: more reliable but cost code size (`-fno-omit-frame-pointer`, `-funwind-tables`).
- **Core dump**: save all RAM to flash and load it into GDB later (Memfault and Zephyr's coredump do this).

## 8.6 Instruction trace and profiling

- **ETM** records every taken branch. With a trace probe (J-Trace, ULINKpro) or the H7's on-chip trace buffer (ETF, 4 KB), you can reconstruct the exact path into a crash: "how did we get here?" questions that a stack snapshot can't answer.
- **PC sampling** (DWT `PCSAMPLENA` over SWO): a statistical profiler with no code changes. Every N cycles the current PC is emitted, and the host builds a function histogram.
- **Cycle counting with DWT** (Modules 1, 3, 4) remains the most precise profiler for a known code region.

## Knowledge check 8

1. How does a HardFault handler know whether to read the stacked frame from MSP or PSP?
2. What does an imprecise BusFault mean, and why is the stacked PC not the faulting instruction?
3. Name two advantages of SEGGER RTT over semihosting `printf`.
4. Which register holds the faulting address for a precise data-access MemManage fault, and which bit says it is valid?

## Further reading

- Arm, *ARMv7-M Architecture Reference Manual*, B3.2.15–B3.2.18 (CFSR, HFSR, MMFAR, BFAR).
- Arm, *Application Note 209: Using Cortex-M3 and Cortex-M4 Fault Exceptions*.
- Memfault Interrupt blog: "How to debug a HardFault on an ARM Cortex-M MCU".
- SEGGER, *RTT* documentation (wiki.segger.com/RTT).
- ST, *AN4989 STM32 microcontroller debug toolbox*.
