# Glossary

[← Module 8 wiki](README.md)

Short definitions, alphabetical. Each entry links to the page that explains the term in depth. Register addresses are for ARMv7-M and ARMv8-M Mainline (Cortex-M3/M4/M7/M33).

[A–C](#a) · [D–E](#d) · [F–H](#f) · [I–L](#i) · [M–N](#m) · [P–R](#p) · [S](#s) · [T–W](#t)

---

## A

### ACTLR
*Auxiliary Control Register* (`0xE000E008`). Implementation-defined core tuning bits. On the Cortex-M3/M4 it contains [DISDEFWBUF](#disdefwbuf). The Cortex-M7's ACTLR has a different set of bits. → [Precise vs imprecise](precise-imprecise.md)

### addr2line
A binutils tool (`arm-none-eabi-addr2line`) that maps a code address to a function name and file:line, using the DWARF debug information in an ELF file. → [Post-mortem](post-mortem.md)

## B

### BFAR
*BusFault Address Register* (`0xE000ED38`). Holds the data address of a precise bus fault. It's valid only while [BFARVALID](#bfarvalid) is set. → [Fault registers](fault-registers.md)

### BFARVALID
CFSR bit 15. When it's set, [BFAR](#bfar) holds a valid address. It's never set for [imprecise](#impreciserr) faults. → [Fault registers](fault-registers.md)

### BFSR
*BusFault Status Register*: bits 15:8 of the [CFSR](#cfsr). → [Fault registers](fault-registers.md)

### BKPT
The breakpoint instruction. With a debugger attached it halts the core. Without one it escalates to [HardFault](#hardfault) with [HFSR](#hfsr)`.DEBUGEVT` set. [Semihosting](#semihosting) uses `BKPT 0xAB`.

### Build ID
A unique identifier (usually a hash) for one specific build, embedded in the image and reported in every crash record. It tells you exactly which ELF to decode a report with. → [Post-mortem](post-mortem.md#keep-the-exact-elf)

### BusFault
Exception 5. Raised by an error response from the bus: instruction fetch, data access, stacking or unstacking. It's configurable and disabled at reset. → [Fault exceptions](fault-exceptions.md)

## C

### CCR
*Configuration and Control Register* (`0xE000ED14`). Contains `UNALIGN_TRP` (bit 3), which traps all unaligned accesses, and `DIV_0_TRP` (bit 4), which traps integer division by zero. On v7-M it also contains `STKALIGN` (bit 9), which forces 8-byte stack alignment on exception entry. → [Fault exceptions](fault-exceptions.md#optional-traps-in-ccr)

### CFSR
*Configurable Fault Status Register* (`0xE000ED28`). One bit per fault cause, made up of [UFSR](#ufsr)[31:16], [BFSR](#bfsr)[15:8] and [MMFSR](#mmfsr)[7:0]. The bits are write-1-to-clear. This is the first register you read after any fault. → [Fault registers](fault-registers.md)

### CMSIS
*Common Microcontroller Software Interface Standard*: Arm's headers that give register structs (`SCB->CFSR`, `DWT->CYCCNT`), bit masks and intrinsics (`__DSB()`, `__get_IPSR()`).

### CoreSight
Arm's architecture for on-chip debug and trace. On a Cortex-M it includes the DAP, FPB, DWT, ITM, TPIU and (optionally) the ETM. → [CoreSight](coresight.md)

### Core dump
A copy of all (or most) RAM plus the registers, saved at crash time and later loaded into GDB as if the target were halted at the fault. → [Post-mortem](post-mortem.md#core-dumps)

### CRC
*Cyclic redundancy check*. In a crash record, a CRC-32 over the record distinguishes a genuine record from leftover RAM contents after power-on. → [Crash handler](crash-handler.md#the-crash-record)

### Crash record
The structure the fault handler fills in before resetting: the stacked frame, the fault registers, EXC_RETURN, a stack snapshot, and a magic number with a CRC. → [Crash handler](crash-handler.md#the-crash-record)

### CYCCNT
The DWT's free-running 32-bit cycle counter (`DWT->CYCCNT`). At 400 MHz it wraps about every 10.7 s. → [Trace and profiling](trace-profiling.md#cycle-counting-with-the-dwt)

---

## D

### DACCVIOL
CFSR bit 1. A data access violated the [MPU](#mpu) settings. Check [MMARVALID](#mmarvalid) and [MMFAR](#mmfar). → [Fault registers](fault-registers.md)

### DAP
*Debug Access Port*. The probe's way into the chip: SWD (or JTAG) on the outside, an access port onto the system bus on the inside. It reads and writes memory without halting the core. → [CoreSight](coresight.md#dap-access-while-running)

### DebugMonitor
Exception 12. When monitor-mode debug is enabled (`DEMCR.MON_EN`) and no debugger is using halting mode, breakpoints and watchpoints raise this exception instead of halting. Firmware can then handle them itself. → [CoreSight](coresight.md#watchpoints-without-a-debugger-debugmonitor)

### Deferred logging
Logging that sends a numeric ID and raw arguments instead of formatted text. The host keeps a table that maps IDs to format strings. Trice and defmt are examples. → [Logging](logging.md#deferred-binary-logging)

### DEMCR
*Debug Exception and Monitor Control Register* (`0xE000EDFC`). `TRCENA` (bit 24) powers the DWT and ITM. `MON_EN` (bit 16) enables [DebugMonitor](#debugmonitor). The `VC_*` bits enable [vector catch](#vector-catch).

### DHCSR
*Debug Halting Control and Status Register* (`0xE000EDF0`). `C_DEBUGEN` says halting debug is enabled. `S_LOCKUP` says the core is in [lockup](#lockup).

### DISDEFWBUF
ACTLR bit 1 on the Cortex-M3/M4. It disables write buffering for the default memory map, which makes every bus fault precise at a performance cost. The Cortex-M7 doesn't have it. → [Precise vs imprecise](precise-imprecise.md#on-cortex-m3m4-disdefwbuf)

### DIVBYZERO
CFSR bit 25. An integer divide by zero, reported only when `CCR.DIV_0_TRP` is set. Otherwise `SDIV`/`UDIV` by zero returns 0.

### DSB
*Data Synchronization Barrier*. Stalls until all earlier memory accesses have completed. It's used to make a buffered store's bus error show up at a known place. → [Precise vs imprecise](precise-imprecise.md#on-cortex-m7-bisect-with-dsb)

### DWT
*Data Watchpoint and Trace* unit. Provides the cycle counter, (usually) 4 comparators for watchpoints, PC sampling, and exception trace. On the M7 it has a CoreSight lock (`DWT->LAR = 0xC5ACCE55`). → [CoreSight](coresight.md#dwt-watchpoints-and-counters)

---

## E

### ELF
The linker's output file. It holds the code plus the symbols and DWARF debug info that `addr2line`, `objdump` and GDB need. Keep the ELF of every build you release. → [Post-mortem](post-mortem.md#keep-the-exact-elf)

### Escalation
When a configurable fault can't be taken as its own exception, it's taken as a HardFault instead. That happens if the fault class is disabled, or if the fault occurs at a priority equal to or higher than its own handler's. `HFSR.FORCED` records that it happened. → [Fault exceptions](fault-exceptions.md#escalation)

### ETB
*Embedded Trace Buffer*: an on-chip RAM that captures trace. The STM32H7 uses an [ETF](#etf) for the same job.

### ETF
*Embedded Trace FIFO*. On the STM32H7, a 4 KB on-chip buffer that captures ETM trace without an external trace probe. → [Trace and profiling](trace-profiling.md#etm-instruction-trace)

### ETM
*Embedded Trace Macrocell*. Emits a compressed record of every branch the core takes, so the host can reconstruct the full instruction path. → [Trace and profiling](trace-profiling.md#etm-instruction-trace)

### EXC_RETURN
The special value the core loads into LR on exception entry (`0xFFFFFFxx`). Branching to it returns from the exception. Bit 2 selects [MSP or PSP](#msp--psp). Bit 4 = 0 means an extended (FP) frame. → [Exception frame](exception-frame.md#exc_return)

---

## F

### FAULTMASK
A core register that, when set, raises the execution priority to −1. That masks every exception except NMI. Fault handlers sometimes use it.

### FORCED
HFSR bit 30. A configurable fault was [escalated](#escalation) to HardFault. The real cause is in the CFSR. → [Fault registers](fault-registers.md#hfsr)

### FPB
*Flash Patch and Breakpoint* unit. Provides the hardware breakpoints (6–8 on M3/M4/M7, 8 on M33). On v7-M it can also remap flash addresses ("flash patching"). → [CoreSight](coresight.md#fpb-hardware-breakpoints)

### Frame
See [stacked frame](#stacked-frame).

## H

### HardFault
Exception 3, at fixed priority −1. It's the target for every fault that can't be handled by its own handler. It also catches vector-table read errors and debug events with no debugger attached. → [Fault exceptions](fault-exceptions.md)

### HFSR
*HardFault Status Register* (`0xE000ED2C`). `VECTTBL` (bit 1) means a bus error on a vector fetch. `FORCED` (bit 30) means a fault was escalated. `DEBUGEVT` (bit 31) means a debug event with no debugger. → [Fault registers](fault-registers.md#hfsr)

---

## I

### IACCVIOL
CFSR bit 0. An instruction fetch from an address the MPU forbids, or from an execute-never region (for example the System region at `0xE0000000`+, or the Device region). MMFAR is *not* valid for this bit. → [Fault registers](fault-registers.md)

### IBUSERR
CFSR bit 8. A bus error on instruction fetch, typically from jumping to an address that isn't backed by memory.

### IMPRECISERR
CFSR bit 10. A data bus error that was reported after the faulting store had already retired. The stacked PC is *later* than the culprit, and BFAR isn't valid. → [Precise vs imprecise](precise-imprecise.md)

### INVPC
CFSR bit 18. An invalid EXC_RETURN value was used on exception return. This usually means a corrupted stack inside a handler.

### INVSTATE
CFSR bit 17. The core tried to execute in ARM state, which Cortex-M doesn't support. This happens after a branch to an address with bit 0 = 0, typically a null or corrupted function pointer. → [Crash signatures](crash-signatures.md)

### IPSR
*Interrupt Program Status Register*: the low bits of xPSR, holding the active exception number (3 = HardFault, 4 = MemManage, 5 = BusFault, 6 = UsageFault). Read it with `__get_IPSR()`.

### ITM
*Instrumentation Trace Macrocell*. Has 32 stimulus ports. Firmware writes a byte, halfword or word to a port and it goes out as a trace packet over SWO. `ITM_SendChar()` uses port 0. → [Logging](logging.md#itm-over-swo)

## L

### Lazy stacking
FP context preservation where the core reserves space for S0–S15/FPSCR on exception entry but only writes them if the handler actually uses the FPU. Faults during that late write set `MLSPERR`/`LSPERR`. → [Exception frame](exception-frame.md#the-extended-fp-frame)

### Live watch
A debugger feature that reads variables over SWD while the core runs (no halt), refreshed tens of times a second. → [CoreSight](coresight.md#dap-access-while-running)

### Lockup
The state the core enters when it faults while it can't take a fault, for example inside HardFault or NMI, or when a vector fetch fails at priority −1. The core stops executing. Only a reset, a debugger or a watchdog gets it out. `DHCSR.S_LOCKUP` shows it. → [Fault exceptions](fault-exceptions.md#lockup)

### LSPERR
CFSR bit 13. A bus fault during [lazy](#lazy-stacking) FP state preservation.

---

## M

### MemManage
Exception 4. Raised by MPU violations, and by execute-never violations of the default memory map. It's configurable and disabled at reset. → [Fault exceptions](fault-exceptions.md)

### MLSPERR
CFSR bit 5. An MPU fault during lazy FP state preservation.

### MMARVALID
CFSR bit 7. When it's set, [MMFAR](#mmfar) holds the faulting data address.

### MMFAR
*MemManage Fault Address Register* (`0xE000ED34`). Holds the data address of a MemManage fault, valid only while [MMARVALID](#mmarvalid) is set. On some cores it shares storage with BFAR. → [Fault registers](fault-registers.md#mmfar-and-bfar)

### MMFSR
*MemManage Fault Status Register*: bits 7:0 of the [CFSR](#cfsr).

### MPU
*Memory Protection Unit*. Defines regions with access permissions and memory attributes. Violations raise MemManage. Used for stack guards, execute-never data and making memory Device-type. See [Module 9](../../09-security/README.md).

### MSP / PSP
*Main* and *Process Stack Pointers*. Handlers always use MSP. Thread code uses MSP or PSP (under an RTOS, tasks use PSP). [EXC_RETURN](#exc_return) bit 2 says which one the faulting code was using. → [Exception frame](exception-frame.md#which-stack-msp-or-psp)

### MSTKERR / MUNSTKERR
CFSR bits 4 and 3. An MPU fault while pushing (stacking) or popping (unstacking) the exception frame. `MSTKERR` is the classic sign of a stack overflowing into an MPU guard region.

## N

### Naked function
`__attribute__((naked))`: a function with no compiler-generated prologue or epilogue, so it can only contain inline assembly. The fault entry is naked so that nothing touches a possibly broken stack before you've looked at it. → [Crash handler](crash-handler.md#the-naked-entry)

### NOCP
CFSR bit 19. A coprocessor (FPU) instruction ran while the FPU was disabled (`CPACR`). This usually means early startup code or a library used floating point before the FPU was enabled.

### NOLOAD / noinit
A linker output section marked `(NOLOAD)` and not listed for zeroing by startup. Its contents survive a warm reset. The course BSP provides `NOINIT` (`.noinit`, DTCM) and `SRAM4_NOINIT` (`.sram4_noinit`, SRAM4). → [Crash handler](crash-handler.md#where-the-record-lives)

---

## P

### PC sampling
DWT feature (`DWT_CTRL.PCSAMPLENA`) that emits the current PC as a trace packet every N cycles. The host builds a statistical profile from those samples. → [Trace and profiling](trace-profiling.md#pc-sampling)

### PRECISERR
CFSR bit 9. A data bus error reported synchronously: the stacked PC *is* the faulting instruction, and BFAR is valid.

### Probe
The USB-to-SWD adapter: ST-LINK (on the Nucleo), J-Link, CMSIS-DAP and so on. Trace probes (J-Trace, ULINKpro) can also capture the parallel trace port.

## R

### RTT
*SEGGER Real-Time Transfer*. Ring buffers in target RAM that the probe reads and writes through the DAP while the core runs. It's fast, bidirectional and needs no pins. → [Logging](logging.md#segger-rtt)

---

## S

### Semihosting
A mechanism where the target executes `BKPT 0xAB` and the debugger services the request (print, file I/O) on the host. It halts the core on every call, and HardFaults with no debugger attached. → [Logging](logging.md#semihosting)

### SFSR / SFAR
*SecureFault Status* and *Address Registers* on ARMv8-M with TrustZone. See [Module 9](../../09-security/README.md).

### SHCSR
*System Handler Control and State Register* (`0xE000ED24`). `MEMFAULTENA` (bit 16), `BUSFAULTENA` (bit 17) and `USGFAULTENA` (bit 18) enable the configurable fault handlers. `SECUREFAULTENA` (bit 19) does the same on v8-M. → [Fault exceptions](fault-exceptions.md#enable-them)

### Stack snapshot
A copy of the words just above the stacked frame. Values that fall within `.text` are likely return addresses, which gives you a rough backtrace. → [Post-mortem](post-mortem.md#backtraces)

### Stacked frame
The eight words the core pushes on exception entry: R0, R1, R2, R3, R12, LR, PC, xPSR. With FP context active it's 26 words. → [Exception frame](exception-frame.md)

### STKERR / UNSTKERR
CFSR bits 12 and 11. A bus error while stacking or unstacking the frame. `STKERR` usually means SP pointed outside RAM.

### STKOF
CFSR bit 20 (ARMv8-M only). The stack pointer went below MSPLIM/PSPLIM, which is a hardware stack-overflow check.

### Stimulus port
One of the [ITM](#itm)'s 32 write-only registers. A write to it becomes a trace packet tagged with the port number.

### SWD
*Serial Wire Debug*: Arm's two-wire debug interface (SWDIO, SWCLK). It replaces 4/5-wire JTAG on Cortex-M.

### SWO
*Serial Wire Output*: a single trace pin (PB3 on the STM32H7) that carries ITM/DWT packets to the probe at up to a few Mbit/s. → [Logging](logging.md#itm-over-swo)

---

## T

### TPIU
*Trace Port Interface Unit*. Formats trace data for output, either over SWO or a 4-bit parallel trace port. Its prescaler sets the SWO bit rate.

### TRCENA
DEMCR bit 24. It must be set before the DWT or ITM work at all.

## U

### UFSR
*UsageFault Status Register*: bits 31:16 of the [CFSR](#cfsr).

### UNALIGNED
CFSR bit 24. An unaligned access that trapped, either because `CCR.UNALIGN_TRP` is set or because the instruction (LDM/STM/LDRD/STRD) never supports unaligned addresses.

### UNDEFINSTR
CFSR bit 16. The core decoded an undefined instruction. This usually means corrupted code or execution that wandered into data.

### UsageFault
Exception 6. Raised by instruction-level problems: undefined instruction, invalid state, invalid EXC_RETURN, no coprocessor, unaligned, divide-by-zero, and (v8-M) stack overflow. It's configurable and disabled at reset. → [Fault exceptions](fault-exceptions.md)

## V

### VECTTBL
HFSR bit 1. A bus error while reading the vector table during exception entry. Check VTOR, and check that the table is where you think it is.

### Vector catch
Debugger feature (`DEMCR.VC_HARDERR` and others) that halts the core on entry to a given exception, before the handler runs. It's the fastest way to look at a HardFault live. → [CoreSight](coresight.md#vector-catch)

### VTOR
*Vector Table Offset Register* (`0xE000ED08`). The address of the active vector table.

## W

### Watchpoint
A DWT comparator configured to halt the core (or raise DebugMonitor) when a given address is read or written, or when a given PC is reached. GDB commands: `watch`, `rwatch`, `awatch`. → [CoreSight](coresight.md#dwt-watchpoints-and-counters)

### Write buffer
A small queue between the core and the bus. A store retires as soon as it's queued, and the core keeps executing while the bus completes it. This is the cause of [imprecise](#impreciserr) bus faults. → [Precise vs imprecise](precise-imprecise.md)

## X

### xPSR
The combined program status register: flags (APSR), the exception number ([IPSR](#ipsr)) and execution state (EPSR, including the Thumb bit T). Stacked as the frame's last word. Bit 24 (T) must be 1. Bit 9 of the stacked value says whether 4 bytes of alignment padding were inserted above the frame. → [Exception frame](exception-frame.md#alignment-padding)
