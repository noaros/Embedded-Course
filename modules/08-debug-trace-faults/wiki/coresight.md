# CoreSight debug blocks

[← Module 8 wiki](README.md) · Lecture notes: [§8.1](../README.md#81-coresight-on-cortex-m)

A Cortex-M has a small set of debug and trace blocks, collectively called **CoreSight**. Most engineers only use one feature, the breakpoint, through an IDE. Knowing what each block does lets you pick the right tool for each bug.

```
          ┌──────────── Cortex-M core ────────────┐
 SWDIO ──►│ DAP ──► memory, core regs (no halt)   │
 SWCLK ──►│ FPB  : breakpoints                    │
          │ DWT  : CYCCNT, watchpoints, PC samples├──┐
          │ ITM  : 32 stimulus ports              ├──┤ trace packets
          │ ETM  : every taken branch             ├──┤
          └───────────────────────────────────────┘  ▼
                                    TPIU / SWO ──► SWO pin (1 wire)
                                               └─► TRACED0-3 + TRACECLK
                                    ETF (H7)   ──► on-chip 4 KB buffer
```

## DAP: access while running

The **Debug Access Port** has two halves:

- The **debug port** (SW-DP) talks SWD to the probe.
- An **access port** (AHB-AP) makes bus transactions into the system memory map.

The AHB-AP acts as a second bus master alongside the core, so the probe can read RAM and peripheral registers **without halting the core**.

That's what makes **live watch** work in Cortex-Debug, STM32CubeIDE, Ozone and similar tools. The IDE polls the addresses of watched variables tens of times per second. Uses:

- Counters, state-machine state, error flags: anything slow-changing you'd otherwise `printf`.
- Peripheral registers while running: you can watch a DMA stream's NDTR count down.

Caveats:

- **Cortex-M7 data cache.** The probe reads physical memory over the bus and doesn't see dirty lines still in the D-cache. A watched variable in cached AXI SRAM can look stale. Variables in DTCM (not cached) or in an MPU non-cacheable region are always current. See [Module 5](../../05-concurrency-caches/README.md#56-the-cortex-m7-data-cache).
- **Reading some peripheral registers has side effects.** For example, reading a UART data register pops the FIFO. Don't live-watch those.
- **Low-power modes** can switch off the debug clock. Set the `DBGMCU` "debug in Stop/Standby" bits while debugging.

## FPB: hardware breakpoints

Code in flash can't be patched with a software `BKPT`, so breakpoints in flash use the **Flash Patch and Breakpoint** unit's comparators. The Cortex-M3/M4 have 6 instruction comparators, the M7 has up to 8, and the M33 has 8. When you run out, GDB reports "cannot insert breakpoint". Delete some, or use temporary breakpoints (`tbreak`).

A **conditional breakpoint** (`break foo.c:42 if n > 100`) still uses one comparator. The core halts *every* time it reaches that line, and GDB checks the condition on the host and resumes if it's false. Each halt costs milliseconds, which is fine for logic bugs and useless for timing bugs.

## DWT: watchpoints and counters

The **Data Watchpoint and Trace** unit contains:

| Feature | Register | Use |
| --- | --- | --- |
| Cycle counter | `CYCCNT` | precise timing ([Trace and profiling](trace-profiling.md#cycle-counting-with-the-dwt)) |
| Comparators (usually 4) | `COMPn`, `FUNCTIONn` (+ `MASKn` on v7-M) | watchpoints on a data address or a PC, or an event trigger |
| PC sampling | `CTRL.PCSAMPLENA` | statistical profiling over SWO |
| Exception trace | `CTRL.EXCTRCENA` | logs every exception entry and exit over SWO |
| Event counters | `CPICNT`, `EXCCNT`, `SLEEPCNT`, `LSUCNT`, `FOLDCNT` | coarse "where did the cycles go" statistics |

Before any of it works, set `DCB->DEMCR.TRCENA`. On the M7, also unlock it with `DWT->LAR = 0xC5ACCE55` (see `bsp/common/dwt.h`).

### Watchpoints

A watchpoint answers **"who wrote this variable?"**. Breakpoints can't answer that, because the write might come from any of a hundred places, an ISR or a DMA. (DMA writes don't trigger watchpoints: the DWT only sees the core's own accesses.)

```gdb
(gdb) watch g_state                    # halt on write
(gdb) rwatch g_rx_count                # halt on read
(gdb) awatch *(uint32_t *)0x24001000   # halt on either
(gdb) watch g_state if g_state == 3    # condition checked on the host after each write
```

When it fires, the PC is just *after* the writing instruction (data watchpoints are imprecise by one or two instructions on most cores). `bt` shows you who did it.

On v7-M, the comparator matches an address range of a power-of-two size, set through `MASK`. Watching a whole struct or buffer works if it's suitably aligned. Most debuggers set this up for you when you watch a larger object.

### Watchpoints without a debugger: DebugMonitor

In the field, you can still catch a corrupting write:

1. Enable monitor mode: `DCB->DEMCR |= DCB_DEMCR_MON_EN_Msk | DCB_DEMCR_TRCENA_Msk`.
2. Program a DWT comparator with the address and a "write" function.
3. Implement `DebugMon_Handler()`. When the comparator fires, the exception is taken. The stacked PC (a few instructions past the write) identifies the writer. Record it in the crash record, or log it.

Monitor mode only works while **halting debug is off** (`DHCSR.C_DEBUGEN = 0`). With a debugger attached in the normal way, the core halts instead. Set the DebugMonitor priority in the NVIC so it can preempt the code that does the write.

This is Lab 8's first stretch goal.

### Vector catch

`DEMCR.VC_HARDERR` (and `VC_BUSERR`, `VC_MMERR` and others) makes the debugger **halt on entry** to that fault, before your handler runs, with the stacked frame untouched. OpenOCD and most IDEs turn on `VC_HARDERR` by default. That's why, with a debugger attached, a crash usually stops at the start of the HardFault handler.

## ITM and TPIU/SWO

The **ITM** has 32 stimulus ports. A write to `ITM->PORT[n]` turns into a trace packet. The **TPIU** serialises those packets, together with DWT packets, out of the **SWO** pin.

See [Logging → ITM over SWO](logging.md#itm-over-swo) for setup and costs.

## ETM

The **Embedded Trace Macrocell** records instruction flow. See [Trace and profiling → ETM](trace-profiling.md#etm-instruction-trace).

## Quick reference: which tool for which question

| Question | Tool |
| --- | --- |
| What is this variable doing over time? | live watch (DAP) or RTT log |
| Who writes this variable? | DWT watchpoint (`watch`), or DebugMonitor in the field |
| Why did we end up in HardFault? | vector catch, then read the fault registers ([Fault registers](fault-registers.md)) |
| How long does this function take? | DWT `CYCCNT` |
| Where does the CPU spend its time overall? | PC sampling |
| How exactly did execution get here? | ETM trace |
| What crashed in the field? | crash record ([Crash handler](crash-handler.md)) |
