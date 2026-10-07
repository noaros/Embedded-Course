# Lab 3: Latency budget

**Board:** NUCLEO-H723ZG · **Time:** 3 h · **Builds on:** Module 3, Module 2 (VTOR, ITCM)

First you measure interrupt entry latency and see what moves it. Then you take a deliberately naive interrupt design and rework it until a 20 kHz control loop jitters less than **200 ns** while background interrupts and a "legacy" critical section keep running.

## Hardware

- One jumper wire: **PD12** (TIM4_CH1, morpho connector CN10) → **PA3** (Arduino A0). TIM4 generates the test edge.
- Optional: a logic analyser on PD12 (edge) and **PE9** (Arduino D6, toggled by the EXTI handler) to cross-check the self-timed numbers.

The firmware measures itself: each ISR reads its timer's `CNT` as its first instruction, and the count since the timer's update event is the latency, in 5 ns ticks. Every second it prints min/max/jitter for both ISRs.

## Console commands (115200 baud)

| Key | Toggles |
| --- | --- |
| `v` | TIM2/EXTI3 handlers in ITCM via a vector table in DTCM, or the flash versions |
| `f` | Floating-point work in the main loop (keeps an FP context live) |
| `h` | Floating-point work inside the EXTI handler |
| `n` | TIM3 "noise" ISR: 15 µs of busy work at 3.1 kHz |
| `c` | A 10 µs PRIMASK critical section in the main loop |

USART3 RX interrupts run whenever you type.

## What you hand in

1. **Part A table**: EXTI min/max latency in ns for each combination of `v`, `f`/`h` and `n`, with everything else off. Explain each difference.
2. **Part B before/after**: CONTROL jitter with all load on, in the starter configuration and in your reworked one. The target is < 200 ns.
3. Your **interrupt priority map** (the table format from Module 3, "Designing an interrupt priority map") with a worst-case blocking estimate per level.
4. Your answer to: why does the EXTI minimum latency never go below roughly 100–150 ns, even from ITCM? (Hint: count the stages between the pin and the NVIC.)

## Steps

### Part A: entry latency (75 min)

1. **TODO 1: `exti_init()`.** Route PA3 to EXTI line 3 (SYSCFG `EXTICR`), set the rising-edge trigger and unmask the line for the CPU. Check that the EXTI line prints samples.
2. **TODO 2: `use_ram_vectors()`.** Copy the flash vector table to a DTCM array, patch in the ITCM handlers, and switch `VTOR`. Answer the question in the comment about the 1024-byte alignment.
3. Turn `n`, `c`, `f` and `h` off. Record EXTI latency with `v` off and on.
4. Turn `f` and `h` on. Lazy stacking means the extra cost only appears once the handler executes an FP instruction. Is it visible in the *entry* latency? Is it visible in the time until `gpio_low()` on the logic analyser?
5. Turn `n` on with the starter's priority map (all equal). The EXTI maximum should now be about 15 µs. Explain why in one sentence.

### Part B: rework the design (90 min)

With all load on (`n`, `c`, `f`, `h` on), the starter's CONTROL jitter is tens of microseconds. Fix it without removing any of the load:

- **TODO 3: `apply_priority_map()`.** Give each source a priority based on its deadline and duration.
- **The legacy critical section.** It only protects data shared with low-priority ISRs. Replace PRIMASK with a BASEPRI-based section that leaves your top priorities unmasked (Module 3 §3.5).
- **Code placement.** Measure whether the control loop needs `v` on once the priorities are right. With everything else fixed, what does the I-cache cost you in maximum latency?

Measure for at least 60 s after each change, and keep a log of what moved the number.

## Stretch goals

- Replace the TIM4 pulse with a real external generator at a non-harmonic frequency (for example 997 Hz), so the edge phase sweeps across everything else. Does the maximum change?
- Make the control loop's FP use conditional (`h` equivalent for TIM2), and measure the total ISR duration with and without FP, using DWT at entry and exit.
- Enable USB CDC with TinyUSB and add it to the load. Where does the USB ISR go in your priority map?

## Grading

| Criterion | Points |
| --- | --- |
| EXTI and VTOR relocation working | 3 |
| Part A table complete and explained | 4 |
| Part B jitter < 200 ns with all load on | 4 |
| Priority map with blocking analysis | 3 |
| Answer on the minimum latency floor | 1 |
| **Total** | **15** |
