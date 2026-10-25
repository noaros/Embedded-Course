# Instruction trace and profiling

[← Module 8 wiki](README.md) · Lecture notes: [§8.6](../README.md#86-instruction-trace-and-profiling)

A crash record tells you where the core *was*. Sometimes you need to know **how it got there**: which branch of a state machine ran, which ISR preempted what, or why an "impossible" path executed. A stack snapshot can't answer that. Trace can.

## ETM instruction trace

The **Embedded Trace Macrocell** watches the core's instruction stream and emits compressed packets, mostly "branch taken / not taken" and the occasional full address. The host decoder replays them against the ELF to rebuild every instruction executed. This is non-intrusive: it runs at full speed with no code changes.

Where the trace goes:

| Sink | Capacity | Needs |
| --- | --- | --- |
| **Parallel trace port** (TRACECLK + TRACED0–3) | unlimited streaming; the probe has a large buffer | a trace probe (J-Trace, ULINKpro, Lauterbach), the trace pins routed to a 20-pin connector |
| **ETF / ETB** (on-chip RAM) | 4 KB on the STM32H7, i.e. the last few thousand branches | only an SWD probe to read it out afterwards |

The **ETF in circular mode** acts as a flight recorder: it always holds the most recent history. Stop it on the fault, and it contains the path that led to the crash. Debuggers that support it (Ozone, Lauterbach, Arm Development Studio, some OpenOCD setups) can decode it.

Typical uses:

- "How did we get into this function with this state?"
- Interrupt-ordering problems: who preempted whom, and when.
- **Code coverage** on target, without instrumentation.
- Exact profiling: every instruction is accounted for.

The STM32 Nucleo boards don't route the parallel trace pins to a trace connector, so on a Nucleo the ETF is the practical option.

## PC sampling

The DWT can emit the current PC as a trace packet periodically, over SWO:

- `DWT->CTRL.PCSAMPLENA` enables it. `CYCTAP` and `POSTPRESET` set the interval (every 64 × (n+1) or 1024 × (n+1) cycles).
- The host (Ozone, STM32CubeIDE SWV, `orbuculum` and `orbtop`, pyOCD) collects the samples and maps them through the ELF into a **function histogram**: "41% in `fir_filter`, 18% in `memcpy`, 12% in the idle loop".

It's statistical, so it needs many samples to be accurate. It costs nothing on the target and needs no code changes. Too short an interval overflows the SWO link (packets are dropped and counted as overflow), so match it to the SWO bandwidth.

Combined with `EXCTRCENA` (exception trace), the same stream also shows every interrupt entry and exit, which gives you an ISR timeline.

## Cycle counting with the DWT

For a known region of code, `CYCCNT` is the most precise profiler available:

```c
#include "dwt.h"            /* bsp/common: enables TRCENA, unlocks on M7, starts CYCCNT */

dwt_init();
__DSB(); __ISB();
uint32_t t0 = dwt_cycles();
fir_filter(buf, n);
__DSB(); __ISB();
uint32_t cycles = dwt_cycles() - t0;   /* correct across one wrap (unsigned arithmetic) */
```

- It counts core cycles at full resolution (2.5 ns at 400 MHz).
- The barriers keep the reads from being reordered around the code under test (see Module 1 and [Module 3 §3.8](../../03-exceptions-nvic/README.md#38-measuring-latency)).
- It's a 32-bit counter, so it wraps every ~10.7 s at 400 MHz.
- Interrupts that land inside the region inflate the count. Measure many runs and take the minimum or a histogram, or mask interrupts if that's what you want to measure.

You can also toggle a GPIO at the start and end and use an oscilloscope or logic analyser. That's the only option for measuring end-to-end latency from an external event.

## Which profiler when

| Question | Tool |
| --- | --- |
| How long does *this* function take? | DWT `CYCCNT` |
| Where does the CPU spend its time overall? | PC sampling |
| What's the ISR timeline? | DWT exception trace over SWO, or GPIO toggles + a logic analyser |
| Exactly what code ran before the crash? | ETM → ETF (on-chip) or a trace probe |
| What's my test coverage on target? | ETM trace coverage |
