# Module 8 wiki: debugging, tracing and fault analysis

This wiki goes with [Module 8's lecture notes](../README.md) and [Lab 8: Crash detective](../lab/README.md). This page is the map: it gives you the main ideas in a few lines each, and links to a deeper page for each topic. Terms in **bold links** go to the [glossary](glossary.md).

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

Every Cortex-M carries a set of **[CoreSight](glossary.md#coresight)** blocks. The **[DAP](glossary.md#dap)** lets a probe read and write memory *while the core runs*. The **[FPB](glossary.md#fpb)** provides hardware breakpoints. The **[DWT](glossary.md#dwt)** provides a cycle counter and **[watchpoints](glossary.md#watchpoint)**. The **[ITM](glossary.md#itm)** provides `printf`-style stimulus ports, and the **[ETM](glossary.md#etm)** records every branch.

Watchpoints answer "who wrote this variable?", a question a breakpoint can't answer. The **[DebugMonitor](glossary.md#debugmonitor)** exception lets firmware take watchpoint hits itself, with no debugger attached.

→ More: [CoreSight debug blocks](coresight.md)

## 2. Logging changes the timing you're debugging

A blocking UART `printf` at 115200 baud costs about 87 µs per character. Cheaper options:

- **[ITM](glossary.md#itm)** over **[SWO](glossary.md#swo)**: a few cycles per word, one pin.
- **[SEGGER RTT](glossary.md#rtt)**: a `memcpy` into a RAM ring buffer that the probe reads in the background. No pins.
- **[Deferred logging](glossary.md#deferred-logging)**: send a format-string ID plus raw arguments, and format on the host.

Avoid **[semihosting](glossary.md#semihosting)** in anything timing-sensitive, because it halts the core on every call.

→ More: [Logging without printf costs](logging.md)

## 3. Turn on the configurable faults

There are four fault classes: **[HardFault](glossary.md#hardfault)**, **[MemManage](glossary.md#memmanage)**, **[BusFault](glossary.md#busfault)** and **[UsageFault](glossary.md#usagefault)**. The last three are *disabled at reset*, so every fault **[escalates](glossary.md#escalation)** to HardFault and the information is hidden behind `HFSR.FORCED`. Set the enable bits in **[SHCSR](glossary.md#shcsr)** in every product. If a fault happens inside the HardFault handler itself, the core enters **[lockup](glossary.md#lockup)**.

→ More: [Fault exceptions and escalation](fault-exceptions.md)

## 4. The status registers say what happened and where

- **[CFSR](glossary.md#cfsr)** says *what* happened: one bit per cause, grouped into MMFSR, BFSR and UFSR.
- **[HFSR](glossary.md#hfsr)** says *why* you ended up in HardFault: escalated (`FORCED`), a bad vector table read, or a debug event.
- **[MMFAR](glossary.md#mmfar)** and **[BFAR](glossary.md#bfar)** say *which data address* was involved, but only when `MMARVALID` or `BFARVALID` is set.

Read CFSR first, then the address registers, then clear CFSR. Its bits are write-1-to-clear.

→ More: [Fault status registers](fault-registers.md)

## 5. Imprecise faults lie about the PC

A store can retire into the **[write buffer](glossary.md#write-buffer)** before the bus reports an error. The fault then arrives several instructions later, flagged **[IMPRECISERR](glossary.md#impreciserr)**, with no BFAR. To pin it down:

- On the M3/M4, set **[DISDEFWBUF](glossary.md#disdefwbuf)**.
- On the M7, which lacks that bit, insert a **[DSB](glossary.md#dsb)** after each suspect store.

→ More: [Precise vs imprecise bus faults](precise-imprecise.md)

## 6. The stacked frame gives you the faulting PC

On exception entry the core pushes R0–R3, R12, LR, PC and xPSR. The stacked **PC** is the faulting instruction (for precise faults), and the stacked **LR** gives you one level of call stack for free. Bit 2 of **[EXC_RETURN](glossary.md#exc_return)** says whether that frame is on the **[MSP or PSP](glossary.md#msp--psp)**.

→ More: [The stacked frame and EXC_RETURN](exception-frame.md)

## 7. A handler that records, then resets

The handler starts as a **[naked function](glossary.md#naked-function)** that picks the right stack and branches to C. The C part follows a few rules:

- Check that the frame pointer is in RAM before reading it.
- Copy the frame and fault registers into a **[crash record](glossary.md#crash-record)**.
- Protect the record with a magic number and a **[CRC](glossary.md#crc)**.
- Store the record in a **[NOLOAD/noinit](glossary.md#noload--noinit)** section that startup doesn't zero.
- Reset.

There's no `printf` and no RTOS call in the handler. Reporting happens on the next boot, when the system is healthy.

→ More: [Writing a crash handler](crash-handler.md)

## 8. From the record to a source line

Run `arm-none-eabi-addr2line -f -e <elf> <pc> <lr>` against the **exact** ELF that was flashed, and read the disassembly around the PC with `objdump -d`. A **[build ID](glossary.md#build-id)** in every crash record ties the report to its ELF. A **[stack snapshot](glossary.md#stack-snapshot)** gives a rough backtrace. A full **[core dump](glossary.md#core-dump)** gives you everything.

→ More: [Post-mortem analysis](post-mortem.md) · [Crash signatures](crash-signatures.md)

## 9. When the stack can't tell you how you got there

**[ETM](glossary.md#etm)** instruction trace, captured by a trace probe or the H7's on-chip **[ETF](glossary.md#etf)**, reconstructs the exact path into a crash. **[PC sampling](glossary.md#pc-sampling)** gives you a statistical profiler with no code changes. The DWT cycle counter remains the most precise way to time a known region of code.

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
