# Module 8 wiki: debugging, tracing and fault analysis

This wiki goes with [Module 8's lecture notes](../README.md) and [Lab 8: Crash detective](../lab/README.md). This page is the map: it gives you the main ideas in a few lines each, and links to a deeper page for each topic. Hover over a <abbr title="Like this one: hovering shows the definition."><b>bold term</b></abbr> to see a short definition. Every term is also in the [glossary](glossary.md), which links to the page that covers it in depth.

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

Every Cortex-M carries a set of <abbr title="Arm's architecture for on-chip debug and trace. On a Cortex-M it includes the DAP, FPB, DWT, ITM, TPIU and (optionally) the ETM."><b>CoreSight</b></abbr> blocks. The <abbr title="Debug Access Port. The probe's way into the chip: SWD (or JTAG) on the outside, an access port onto the system bus on the inside. It reads and writes memory without halting the core."><b>DAP</b></abbr> lets a probe read and write memory *while the core runs*. The <abbr title="Flash Patch and Breakpoint unit. Provides the hardware breakpoints (6–8 on M3/M4/M7, 8 on M33). On v7-M it can also remap flash addresses (&quot;flash patching&quot;)."><b>FPB</b></abbr> provides hardware breakpoints. The <abbr title="Data Watchpoint and Trace unit. Provides the cycle counter, (usually) 4 comparators for watchpoints, PC sampling, and exception trace. On the M7 it has a CoreSight lock (DWT-&gt;LAR = 0xC5ACCE55)."><b>DWT</b></abbr> provides a cycle counter and <abbr title="A DWT comparator configured to halt the core (or raise DebugMonitor) when a given address is read or written, or when a given PC is reached. GDB commands: watch, rwatch, awatch."><b>watchpoints</b></abbr>. The <abbr title="Instrumentation Trace Macrocell. Has 32 stimulus ports. Firmware writes a byte, halfword or word to a port and it goes out as a trace packet over SWO. ITM_SendChar() uses port 0."><b>ITM</b></abbr> provides `printf`-style stimulus ports, and the <abbr title="Embedded Trace Macrocell. Emits a compressed record of every branch the core takes, so the host can reconstruct the full instruction path."><b>ETM</b></abbr> records every branch.

Watchpoints answer "who wrote this variable?", a question a breakpoint can't answer. The <abbr title="Exception 12. When monitor-mode debug is enabled (DEMCR.MON_EN) and no debugger is using halting mode, breakpoints and watchpoints raise this exception instead of halting. Firmware can then handle them itself."><b>DebugMonitor</b></abbr> exception lets firmware take watchpoint hits itself, with no debugger attached.

→ More: [CoreSight debug blocks](coresight.md)

## 2. Logging changes the timing you're debugging

A blocking UART `printf` at 115200 baud costs about 87 µs per character. Cheaper options:

- <abbr title="Instrumentation Trace Macrocell. Has 32 stimulus ports. Firmware writes a byte, halfword or word to a port and it goes out as a trace packet over SWO. ITM_SendChar() uses port 0."><b>ITM</b></abbr> over <abbr title="Serial Wire Output: a single trace pin (PB3 on the STM32H7) that carries ITM/DWT packets to the probe at up to a few Mbit/s."><b>SWO</b></abbr>: a few cycles per word, one pin.
- <abbr title="SEGGER Real-Time Transfer. Ring buffers in target RAM that the probe reads and writes through the DAP while the core runs. It's fast, bidirectional and needs no pins."><b>SEGGER RTT</b></abbr>: a `memcpy` into a RAM ring buffer that the probe reads in the background. No pins.
- <abbr title="Logging that sends a numeric ID and raw arguments instead of formatted text. The host keeps a table that maps IDs to format strings. Trice and defmt are examples."><b>Deferred logging</b></abbr>: send a format-string ID plus raw arguments, and format on the host.

Avoid <abbr title="A mechanism where the target executes BKPT 0xAB and the debugger services the request (print, file I/O) on the host. It halts the core on every call, and HardFaults with no debugger attached."><b>semihosting</b></abbr> in anything timing-sensitive, because it halts the core on every call.

→ More: [Logging without printf costs](logging.md)

## 3. Turn on the configurable faults

There are four fault classes: <abbr title="Exception 3, at fixed priority −1. It's the target for every fault that can't be handled by its own handler. It also catches vector-table read errors and debug events with no debugger attached."><b>HardFault</b></abbr>, <abbr title="Exception 4. Raised by MPU violations, and by execute-never violations of the default memory map. It's configurable and disabled at reset."><b>MemManage</b></abbr>, <abbr title="Exception 5. Raised by an error response from the bus: instruction fetch, data access, stacking or unstacking. It's configurable and disabled at reset."><b>BusFault</b></abbr> and <abbr title="Exception 6. Raised by instruction-level problems: undefined instruction, invalid state, invalid EXC_RETURN, no coprocessor, unaligned, divide-by-zero, and (v8-M) stack overflow. It's configurable and disabled at reset."><b>UsageFault</b></abbr>. The last three are *disabled at reset*, so every fault <abbr title="When a configurable fault can't be taken as its own exception, it's taken as a HardFault instead. That happens if the fault class is disabled, or if the fault occurs at a priority equal to or higher than its own handler's. HFSR.FORCED records that it happened."><b>escalates</b></abbr> to HardFault and the information is hidden behind `HFSR.FORCED`. Set the enable bits in <abbr title="System Handler Control and State Register (0xE000ED24). MEMFAULTENA (bit 16), BUSFAULTENA (bit 17) and USGFAULTENA (bit 18) enable the configurable fault handlers. SECUREFAULTENA (bit 19) does the same on v8-M."><b>SHCSR</b></abbr> in every product. If a fault happens inside the HardFault handler itself, the core enters <abbr title="The state the core enters when it faults while it can't take a fault, for example inside HardFault or NMI, or when a vector fetch fails at priority −1. The core stops executing. Only a reset, a debugger or a watchdog gets it out. DHCSR.S_LOCKUP shows it."><b>lockup</b></abbr>.

→ More: [Fault exceptions and escalation](fault-exceptions.md)

## 4. The status registers say what happened and where

- <abbr title="Configurable Fault Status Register (0xE000ED28). One bit per fault cause, made up of UFSR[31:16], BFSR[15:8] and MMFSR[7:0]. The bits are write-1-to-clear. This is the first register you read after any fault."><b>CFSR</b></abbr> says *what* happened: one bit per cause, grouped into MMFSR, BFSR and UFSR.
- <abbr title="HardFault Status Register (0xE000ED2C). VECTTBL (bit 1) means a bus error on a vector fetch. FORCED (bit 30) means a fault was escalated. DEBUGEVT (bit 31) means a debug event with no debugger."><b>HFSR</b></abbr> says *why* you ended up in HardFault: escalated (`FORCED`), a bad vector table read, or a debug event.
- <abbr title="MemManage Fault Address Register (0xE000ED34). Holds the data address of a MemManage fault, valid only while MMARVALID is set. On some cores it shares storage with BFAR."><b>MMFAR</b></abbr> and <abbr title="BusFault Address Register (0xE000ED38). Holds the data address of a precise bus fault. It's valid only while BFARVALID is set."><b>BFAR</b></abbr> say *which data address* was involved, but only when `MMARVALID` or `BFARVALID` is set.

Read CFSR first, then the address registers, then clear CFSR. Its bits are write-1-to-clear.

→ More: [Fault status registers](fault-registers.md)

## 5. Imprecise faults lie about the PC

A store can retire into the <abbr title="A small queue between the core and the bus. A store retires as soon as it's queued, and the core keeps executing while the bus completes it. This is the cause of imprecise bus faults."><b>write buffer</b></abbr> before the bus reports an error. The fault then arrives several instructions later, flagged <abbr title="CFSR bit 10. A data bus error that was reported after the faulting store had already retired. The stacked PC is later than the culprit, and BFAR isn't valid."><b>IMPRECISERR</b></abbr>, with no BFAR. To pin it down:

- On the M3/M4, set <abbr title="ACTLR bit 1 on the Cortex-M3/M4. It disables write buffering for the default memory map, which makes every bus fault precise at a performance cost. The Cortex-M7 doesn't have it."><b>DISDEFWBUF</b></abbr>.
- On the M7, which lacks that bit, insert a <abbr title="Data Synchronization Barrier. Stalls until all earlier memory accesses have completed. It's used to make a buffered store's bus error show up at a known place."><b>DSB</b></abbr> after each suspect store.

→ More: [Precise vs imprecise bus faults](precise-imprecise.md)

## 6. The stacked frame gives you the faulting PC

On exception entry the core pushes R0–R3, R12, LR, PC and xPSR. The stacked **PC** is the faulting instruction (for precise faults), and the stacked **LR** gives you one level of call stack for free. Bit 2 of <abbr title="The special value the core loads into LR on exception entry (0xFFFFFFxx). Branching to it returns from the exception. Bit 2 selects MSP or PSP. Bit 4 = 0 means an extended (FP) frame."><b>EXC_RETURN</b></abbr> says whether that frame is on the <abbr title="Main and Process Stack Pointers. Handlers always use MSP. Thread code uses MSP or PSP (under an RTOS, tasks use PSP). EXC_RETURN bit 2 says which one the faulting code was using."><b>MSP or PSP</b></abbr>.

→ More: [The stacked frame and EXC_RETURN](exception-frame.md)

## 7. A handler that records, then resets

The handler starts as a <abbr title="__attribute__((naked)): a function with no compiler-generated prologue or epilogue, so it can only contain inline assembly. The fault entry is naked so that nothing touches a possibly broken stack before you've looked at it."><b>naked function</b></abbr> that picks the right stack and branches to C. The C part follows a few rules:

- Check that the frame pointer is in RAM before reading it.
- Copy the frame and fault registers into a <abbr title="The structure the fault handler fills in before resetting: the stacked frame, the fault registers, EXC_RETURN, a stack snapshot, and a magic number with a CRC."><b>crash record</b></abbr>.
- Protect the record with a magic number and a <abbr title="Cyclic redundancy check. In a crash record, a CRC-32 over the record distinguishes a genuine record from leftover RAM contents after power-on."><b>CRC</b></abbr>.
- Store the record in a <abbr title="A linker output section marked (NOLOAD) and not listed for zeroing by startup. Its contents survive a warm reset. The course BSP provides NOINIT (.noinit, DTCM) and SRAM4_NOINIT (.sram4_noinit, SRAM4)."><b>NOLOAD/noinit</b></abbr> section that startup doesn't zero.
- Reset.

There's no `printf` and no RTOS call in the handler. Reporting happens on the next boot, when the system is healthy.

→ More: [Writing a crash handler](crash-handler.md)

## 8. From the record to a source line

Run `arm-none-eabi-addr2line -f -e <elf> <pc> <lr>` against the **exact** ELF that was flashed, and read the disassembly around the PC with `objdump -d`. A <abbr title="A unique identifier (usually a hash) for one specific build, embedded in the image and reported in every crash record. It tells you exactly which ELF to decode a report with."><b>build ID</b></abbr> in every crash record ties the report to its ELF. A <abbr title="A copy of the words just above the stacked frame. Values that fall within .text are likely return addresses, which gives you a rough backtrace."><b>stack snapshot</b></abbr> gives a rough backtrace. A full <abbr title="A copy of all (or most) RAM plus the registers, saved at crash time and later loaded into GDB as if the target were halted at the fault."><b>core dump</b></abbr> gives you everything.

→ More: [Post-mortem analysis](post-mortem.md) · [Crash signatures](crash-signatures.md)

## 9. When the stack can't tell you how you got there

<abbr title="Embedded Trace Macrocell. Emits a compressed record of every branch the core takes, so the host can reconstruct the full instruction path."><b>ETM</b></abbr> instruction trace, captured by a trace probe or the H7's on-chip <abbr title="Embedded Trace FIFO. On the STM32H7, a 4 KB on-chip buffer that captures ETM trace without an external trace probe."><b>ETF</b></abbr>, reconstructs the exact path into a crash. <abbr title="DWT feature (DWT_CTRL.PCSAMPLENA) that emits the current PC as a trace packet every N cycles. The host builds a statistical profile from those samples."><b>PC sampling</b></abbr> gives you a statistical profiler with no code changes. The DWT cycle counter remains the most precise way to time a known region of code.

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
