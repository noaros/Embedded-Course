# Module 8 wiki: debugging, tracing and fault analysis

This wiki goes with [Module 8's lecture notes](../README.md) and [Lab 8: Crash detective](../lab/README.md). This page is the map: it gives you the main ideas in a few lines each, and links to a deeper page for each topic. Hover over a [**bold term**](#tip-bold-term "Like this one: hovering shows the definition.") to see a short definition. Every term is also in the [glossary](glossary.md), which links to the page that covers it in depth.

> **The one-sentence version:** in the field you'll never have a debugger attached, so you build firmware that records *why* it crashed, survives the reset, and reports on the next boot. You then decode that record with the fault status registers and the exact ELF file.

## Pages

| Page | What it covers |
| --- | --- |
| [Glossary](glossary.md) | every acronym and register in the module, one short definition each |
| [CoreSight debug blocks](coresight.md) | DAP, FPB, DWT, ITM, TPIU/SWO, ETM; watchpoints, DebugMonitor, live watch, vector catch |
| [Logging without printf costs](logging.md) | ITM, SEGGER RTT, semihosting, deferred binary logging, with costs |
| [Fault exceptions and escalation](fault-exceptions.md) | the four fault classes, SHCSR, priorities, escalation to HardFault, lockup |
| [Fault status registers](fault-registers.md) | CFSR bit by bit, HFSR, MMFAR/BFAR, the read-then-clear order |
| [Precise vs imprecise bus faults](precise-imprecise.md) | the write buffer, DISDEFWBUF on the M3/M4, DSB bisection on the M7 |
| [The stacked frame and EXC_RETURN](exception-frame.md) | what the core pushes, MSP vs PSP, the FP frame, stack alignment padding |
| [Writing a crash handler](crash-handler.md) | naked entry, robustness rules, the crash record, surviving reset |
| [Post-mortem analysis](post-mortem.md) | addr2line, objdump, keeping the ELF, build IDs, backtraces, core dumps |
| [Crash signatures](crash-signatures.md) | a lookup table: "I see these bits and this PC" → likely root cause |
| [Instruction trace and profiling](trace-profiling.md) | ETM, the on-chip ETF, PC sampling, DWT cycle counting |

---

## 1. The debug hardware is more than breakpoints

Every Cortex-M carries a set of [**CoreSight**](#tip-coresight "Arm's architecture for on-chip debug and trace. On a Cortex-M it includes the DAP, FPB, DWT, ITM, TPIU and (optionally) the ETM.") blocks. The [**DAP**](#tip-dap "Debug Access Port. The probe's way into the chip: SWD (or JTAG) on the outside, an access port onto the system bus on the inside. It reads and writes memory without halting the core.") lets a probe read and write memory *while the core runs*. The [**FPB**](#tip-fpb "Flash Patch and Breakpoint unit. Provides the hardware breakpoints (6–8 on M3/M4/M7, 8 on M33). On v7-M it can also remap flash addresses (&quot;flash patching&quot;).") provides hardware breakpoints. The [**DWT**](#tip-dwt "Data Watchpoint and Trace unit. Provides the cycle counter, (usually) 4 comparators for watchpoints, PC sampling, and exception trace. On the M7 it has a CoreSight lock (DWT-&gt;LAR = 0xC5ACCE55).") provides a cycle counter and [**watchpoints**](#tip-watchpoints "A DWT comparator configured to halt the core (or raise DebugMonitor) when a given address is read or written, or when a given PC is reached. GDB commands: watch, rwatch, awatch."). The [**ITM**](#tip-itm "Instrumentation Trace Macrocell. Has 32 stimulus ports. Firmware writes a byte, halfword or word to a port and it goes out as a trace packet over SWO. ITM_SendChar() uses port 0.") provides `printf`-style stimulus ports, and the [**ETM**](#tip-etm "Embedded Trace Macrocell. Emits a compressed record of every branch the core takes, so the host can reconstruct the full instruction path.") records every branch.

Watchpoints answer "who wrote this variable?", a question a breakpoint can't answer. The [**DebugMonitor**](#tip-debugmonitor "Exception 12. When monitor-mode debug is enabled (DEMCR.MON_EN) and no debugger is using halting mode, breakpoints and watchpoints raise this exception instead of halting. Firmware can then handle them itself.") exception lets firmware take watchpoint hits itself, with no debugger attached.

→ More: [CoreSight debug blocks](coresight.md)

## 2. Logging changes the timing you're debugging

A blocking UART `printf` at 115200 baud costs about 87 µs per character. Cheaper options:

- [**ITM**](#tip-itm "Instrumentation Trace Macrocell. Has 32 stimulus ports. Firmware writes a byte, halfword or word to a port and it goes out as a trace packet over SWO. ITM_SendChar() uses port 0.") over [**SWO**](#tip-swo "Serial Wire Output: a single trace pin (PB3 on the STM32H7) that carries ITM/DWT packets to the probe at up to a few Mbit/s."): a few cycles per word, one pin.
- [**SEGGER RTT**](#tip-segger-rtt "SEGGER Real-Time Transfer. Ring buffers in target RAM that the probe reads and writes through the DAP while the core runs. It's fast, bidirectional and needs no pins."): a `memcpy` into a RAM ring buffer that the probe reads in the background. No pins.
- [**Deferred logging**](#tip-deferred-logging "Logging that sends a numeric ID and raw arguments instead of formatted text. The host keeps a table that maps IDs to format strings. Trice and defmt are examples."): send a format-string ID plus raw arguments, and format on the host.

Avoid [**semihosting**](#tip-semihosting "A mechanism where the target executes BKPT 0xAB and the debugger services the request (print, file I/O) on the host. It halts the core on every call, and HardFaults with no debugger attached.") in anything timing-sensitive, because it halts the core on every call.

→ More: [Logging without printf costs](logging.md)

## 3. Turn on the configurable faults

There are four fault classes: [**HardFault**](#tip-hardfault "Exception 3, at fixed priority −1. It's the target for every fault that can't be handled by its own handler. It also catches vector-table read errors and debug events with no debugger attached."), [**MemManage**](#tip-memmanage "Exception 4. Raised by MPU violations, and by execute-never violations of the default memory map. It's configurable and disabled at reset."), [**BusFault**](#tip-busfault "Exception 5. Raised by an error response from the bus: instruction fetch, data access, stacking or unstacking. It's configurable and disabled at reset.") and [**UsageFault**](#tip-usagefault "Exception 6. Raised by instruction-level problems: undefined instruction, invalid state, invalid EXC_RETURN, no coprocessor, unaligned, divide-by-zero, and (v8-M) stack overflow. It's configurable and disabled at reset."). The last three are *disabled at reset*, so every fault [**escalates**](#tip-escalates "When a configurable fault can't be taken as its own exception, it's taken as a HardFault instead. That happens if the fault class is disabled, or if the fault occurs at a priority equal to or higher than its own handler's. HFSR.FORCED records that it happened.") to HardFault and the information is hidden behind `HFSR.FORCED`. Set the enable bits in [**SHCSR**](#tip-shcsr "System Handler Control and State Register (0xE000ED24). MEMFAULTENA (bit 16), BUSFAULTENA (bit 17) and USGFAULTENA (bit 18) enable the configurable fault handlers. SECUREFAULTENA (bit 19) does the same on v8-M.") in every product. If a fault happens inside the HardFault handler itself, the core enters [**lockup**](#tip-lockup "The state the core enters when it faults while it can't take a fault, for example inside HardFault or NMI, or when a vector fetch fails at priority −1. The core stops executing. Only a reset, a debugger or a watchdog gets it out. DHCSR.S_LOCKUP shows it.").

→ More: [Fault exceptions and escalation](fault-exceptions.md)

## 4. The status registers say what happened and where

- [**CFSR**](#tip-cfsr "Configurable Fault Status Register (0xE000ED28). One bit per fault cause, made up of UFSR[31:16], BFSR[15:8] and MMFSR[7:0]. The bits are write-1-to-clear. This is the first register you read after any fault.") says *what* happened: one bit per cause, grouped into MMFSR, BFSR and UFSR.
- [**HFSR**](#tip-hfsr "HardFault Status Register (0xE000ED2C). VECTTBL (bit 1) means a bus error on a vector fetch. FORCED (bit 30) means a fault was escalated. DEBUGEVT (bit 31) means a debug event with no debugger.") says *why* you ended up in HardFault: escalated (`FORCED`), a bad vector table read, or a debug event.
- [**MMFAR**](#tip-mmfar "MemManage Fault Address Register (0xE000ED34). Holds the data address of a MemManage fault, valid only while MMARVALID is set. On some cores it shares storage with BFAR.") and [**BFAR**](#tip-bfar "BusFault Address Register (0xE000ED38). Holds the data address of a precise bus fault. It's valid only while BFARVALID is set.") say *which data address* was involved, but only when `MMARVALID` or `BFARVALID` is set.

Read CFSR first, then the address registers, then clear CFSR. Its bits are write-1-to-clear.

→ More: [Fault status registers](fault-registers.md)

## 5. Imprecise faults lie about the PC

A store can retire into the [**write buffer**](#tip-write-buffer "A small queue between the core and the bus. A store retires as soon as it's queued, and the core keeps executing while the bus completes it. This is the cause of imprecise bus faults.") before the bus reports an error. The fault then arrives several instructions later, flagged [**IMPRECISERR**](#tip-impreciserr "CFSR bit 10. A data bus error that was reported after the faulting store had already retired. The stacked PC is later than the culprit, and BFAR isn't valid."), with no BFAR. To pin it down:

- On the M3/M4, set [**DISDEFWBUF**](#tip-disdefwbuf "ACTLR bit 1 on the Cortex-M3/M4. It disables write buffering for the default memory map, which makes every bus fault precise at a performance cost. The Cortex-M7 doesn't have it.").
- On the M7, which lacks that bit, insert a [**DSB**](#tip-dsb "Data Synchronization Barrier. Stalls until all earlier memory accesses have completed. It's used to make a buffered store's bus error show up at a known place.") after each suspect store.

→ More: [Precise vs imprecise bus faults](precise-imprecise.md)

## 6. The stacked frame gives you the faulting PC

On exception entry the core pushes R0–R3, R12, LR, PC and xPSR. The stacked **PC** is the faulting instruction (for precise faults), and the stacked **LR** gives you one level of call stack for free. Bit 2 of [**EXC_RETURN**](#tip-exc_return "The special value the core loads into LR on exception entry (0xFFFFFFxx). Branching to it returns from the exception. Bit 2 selects MSP or PSP. Bit 4 = 0 means an extended (FP) frame.") says whether that frame is on the [**MSP or PSP**](#tip-msp-or-psp "Main and Process Stack Pointers. Handlers always use MSP. Thread code uses MSP or PSP (under an RTOS, tasks use PSP). EXC_RETURN bit 2 says which one the faulting code was using.").

→ More: [The stacked frame and EXC_RETURN](exception-frame.md)

## 7. A handler that records, then resets

The handler starts as a [**naked function**](#tip-naked-function "__attribute__((naked)): a function with no compiler-generated prologue or epilogue, so it can only contain inline assembly. The fault entry is naked so that nothing touches a possibly broken stack before you've looked at it.") that picks the right stack and branches to C. The C part follows a few rules:

- Check that the frame pointer is in RAM before reading it.
- Copy the frame and fault registers into a [**crash record**](#tip-crash-record "The structure the fault handler fills in before resetting: the stacked frame, the fault registers, EXC_RETURN, a stack snapshot, and a magic number with a CRC.").
- Protect the record with a magic number and a [**CRC**](#tip-crc "Cyclic redundancy check. In a crash record, a CRC-32 over the record distinguishes a genuine record from leftover RAM contents after power-on.").
- Store the record in a [**NOLOAD/noinit**](#tip-noload-noinit "A linker output section marked (NOLOAD) and not listed for zeroing by startup. Its contents survive a warm reset. The course BSP provides NOINIT (.noinit, DTCM) and SRAM4_NOINIT (.sram4_noinit, SRAM4).") section that startup doesn't zero.
- Reset.

There's no `printf` and no RTOS call in the handler. Reporting happens on the next boot, when the system is healthy.

→ More: [Writing a crash handler](crash-handler.md)

## 8. From the record to a source line

Run `arm-none-eabi-addr2line -f -e <elf> <pc> <lr>` against the **exact** ELF that was flashed, and read the disassembly around the PC with `objdump -d`. A [**build ID**](#tip-build-id "A unique identifier (usually a hash) for one specific build, embedded in the image and reported in every crash record. It tells you exactly which ELF to decode a report with.") in every crash record ties the report to its ELF. A [**stack snapshot**](#tip-stack-snapshot "A copy of the words just above the stacked frame. Values that fall within .text are likely return addresses, which gives you a rough backtrace.") gives a rough backtrace. A full [**core dump**](#tip-core-dump "A copy of all (or most) RAM plus the registers, saved at crash time and later loaded into GDB as if the target were halted at the fault.") gives you everything.

→ More: [Post-mortem analysis](post-mortem.md) · [Crash signatures](crash-signatures.md)

## 9. When the stack can't tell you how you got there

[**ETM**](#tip-etm "Embedded Trace Macrocell. Emits a compressed record of every branch the core takes, so the host can reconstruct the full instruction path.") instruction trace, captured by a trace probe or the H7's on-chip [**ETF**](#tip-etf "Embedded Trace FIFO. On the STM32H7, a 4 KB on-chip buffer that captures ETM trace without an external trace probe."), reconstructs the exact path into a crash. [**PC sampling**](#tip-pc-sampling "DWT feature (DWT_CTRL.PCSAMPLENA) that emits the current PC as a trace packet every N cycles. The host builds a statistical profile from those samples.") gives you a statistical profiler with no code changes. The DWT cycle counter remains the most precise way to time a known region of code.

→ More: [Instruction trace and profiling](trace-profiling.md)

---

## Fault triage in 60 seconds

```
1. Which handler ran? (IPSR: 3 HardFault, 4 MemManage, 5 BusFault, 6 UsageFault)
2. HardFault?  HFSR.FORCED -> look in CFSR.  VECTTBL -> bad VTOR / vector table.
               DEBUGEVT -> a BKPT or semihosting call with no debugger attached.
3. CFSR: which bit is set?          -> fault-registers.md
4. Address valid? (MMARVALID / BFARVALID) -> read MMFAR / BFAR
5. Precise?  addr2line the stacked PC.   Imprecise?  look BEFORE the PC.
6. PC looks wrong (0, 0xA5A5A5A4, odd RAM address)? -> crash-signatures.md
7. addr2line the stacked LR for the caller; scan the stack snapshot for more.
```

## Where this connects to other modules

- [Module 2](../../02-boot-startup-linker/README.md): linker sections (`NOLOAD`, `.noinit`), the vector table, build-ID headers.
- [Module 3](../../03-exceptions-nvic/README.md): exception numbers, the stacked frame, EXC_RETURN, lazy FP stacking.
- [Module 5](../../05-concurrency-caches/README.md): barriers (`DSB`), the M7 cache (it matters for RTT buffers and imprecise faults).
- [Module 9](../../09-security/README.md): the MPU (stack guards → `MSTKERR`), SecureFault, SFSR/SFAR.
- [Module 10](../../10-bootloaders-updates/README.md): watchdog-driven rollback. A crash record tells you *why* the trial boot failed.
