# Lab 7: Coin-cell sensor node

**Board:** NUCLEO-L552ZE-Q · **Time:** 2.5 h · **Builds on:** Module 7

Build a node that wakes every 10 s, reads an I2C temperature/humidity sensor, filters the value, and goes back to sleep. **Target: average MCU current under 5 µA**, with a written energy budget that accounts for every phase.

## Hardware

- An **SHT40/SHT41/SHT45** breakout (Sensirion SHT4x, I2C address 0x44) on I2C1: SCL **PB8** (Arduino D15), SDA **PB9** (D14), 3V3, GND. Any I2C sensor works if you adapt `sht4x_measure()`.
- A **power profiler**: Nordic PPK2 (ammeter mode), ST X-NUCLEO-LPM01A, or similar. Connect it across the **IDD jumper** of the Nucleo (see the board user manual, UM2581), so you measure the MCU supply only.
- Profiler digital input to **PA5** (Arduino D13): the firmware drives it high while awake.
- The LSE crystal (32.768 kHz) is fitted on the Nucleo and drives the RTC.

> **Measuring tips.** Disconnect the ST-LINK's SWD connection, or at least make sure the firmware clears `DBGMCU_CR`, before trusting a Stop number. Measure for at least a minute and read the profiler's **average**, not a spot value.

## Files

```
starter/main.c     the node, with TODO 1–4 (power-related parts only; I2C and the SHT4x protocol are done)
starter/CMakeLists.txt   builds lab07_starter and lab07_starter_debug (keeps SWD alive in Stop)
```

**Do TODO 2 first.** Until the RTC wake-up timer runs, the starter waits forever for its first wake-up.

## What you hand in

1. Profiler screenshots: one full 10 s period, and a zoom on one wake-up showing each phase (use the PA5 marker).
2. **The energy budget table** (template below), filled in from measurements, with the computed average. Compare it with the profiler's average and explain any gap.
3. A **leakage log**: the Stop-current measurement after each change you made, and what you changed (GPIO analog, DBGMCU, probe disconnected, …).
4. Projected battery life on a CR2032 (use 180 mAh usable), and the single change that would extend it most.

### Energy budget template

| Phase | Current (measured) | Duration (measured) | Charge per period |
| --- | --- | --- | --- |
| Wake-up from Stop 2 | | | |
| I2C command | | | |
| Conversion wait (Stop 2) | | | |
| I2C read + CRC + filter | | | |
| Report print (1 in 6 periods) | | | ÷ 6 |
| Stop 2 until next period | | | |
| **Average current** | | | |

## Steps

1. **TODO 2, the RTC wake-up timer.** Get the 10 s period working with the busy-wait still in place. The current is high, but the timing should be right.
2. **TODO 3, Stop 2 entry.** Implement race-free entry. The marker should now show 10 s of low with short high pulses. Measure.
3. **TODO 1, GPIO analog.** Measure the Stop current before and after.
4. **TODO 4, debug in Stop.** Measure with `lab07_starter_debug` and the production build, each with the probe connected and disconnected. Explain the four numbers.
5. **Tune the active phases.** The firmware sleeps through the SHT4x conversion instead of busy-waiting. Measure what that saves compared with a 2 ms busy-wait.
6. Fill in the budget and compare it with the profiler's average.

## Stretch goals

- **Race to sleep.** Run the active phase from MSI at 4 MHz (range 2) and from HSI16. Which costs less **charge** per wake-up?
- **Standby instead of Stop 2.** Keep the log in SRAM2 (`PWR_CR3.RRS`) and the filter state in RTC backup registers. Detect a Standby wake-up with `PWR_SR1.SBF`. Compare the averages. At what period does Standby win?
- **FreeRTOS tickless idle** (Module 7 §7.5) with LPTIM1 on LSE: port the node to a FreeRTOS task with `vTaskDelayUntil(10 s)`, and get within 1 µA of the bare-metal average.

## Grading

| Criterion | Points |
| --- | --- |
| RTC wake-up and race-free Stop 2 entry working | 3 |
| Average current < 5 µA, measured | 4 |
| Energy budget complete, matches measurement within 20% | 4 |
| Leakage log with at least three measured changes | 2 |
| Battery-life projection and recommendation | 1 |
| **Total** | **14** |
