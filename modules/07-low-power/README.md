# Module 7: Low-power design

**Time:** 4 h (1.5 h theory, 2.5 h lab) · **Board:** NUCLEO-L552ZE-Q · **Lab:** [Coin-cell sensor node](lab/README.md)

The goal is a firmware architecture where the default state is asleep and every microamp is accounted for. Low power isn't a feature you add at the end: it decides the clock tree, the interrupt design, the RTOS configuration and the GPIO setup of every pin.

## Learning objectives

1. Use WFI/WFE, SLEEPONEXIT and SLEEPDEEP, and map them to the STM32 Sleep, Stop 0/1/2, Standby and Shutdown modes.
2. Integrate low power with an RTOS via tickless idle and an LPTIM wake-up source.
3. Measure and budget current with a power profiler.
4. Avoid the classic leakage culprits: floating GPIOs, the debug interface left enabled, peripheral clocks left on.

---

## 7.1 The core's side: WFI, WFE, SLEEPDEEP, SLEEPONEXIT

| Mechanism | What it does |
| --- | --- |
| `WFI` | sleep until an interrupt is pending (even if masked by PRIMASK, which is the trick that makes race-free sleep possible) |
| `WFE` | sleep until an **event**: an interrupt, `SEV` from another core, or a peripheral event with `SEVONPEND`. If the event register is already set, it clears it and returns immediately |
| `SCR.SLEEPDEEP` | 0 = "sleep" (core clock stopped only); 1 = "deep sleep", which the vendor maps to Stop/Standby/Shutdown |
| `SCR.SLEEPONEXIT` | on return from the last active ISR, go straight back to sleep instead of returning to Thread mode |
| `SCR.SEVONPEND` | a newly pending interrupt counts as an event for `WFE`, even if it's disabled in the NVIC |

**The race-free sleep pattern.** A naive `if (!work) __WFI();` has a race: the interrupt that sets `work` can fire between the check and the `WFI`, and then you sleep with work pending. The fix uses the fact that WFI wakes on a pending interrupt even when PRIMASK masks it:

```c
__disable_irq();
if (!work_pending()) {
    __DSB();
    __WFI();          /* wakes on any pending IRQ, even with PRIMASK set */
}
__enable_irq();       /* the ISR runs here */
```

**SLEEPONEXIT** turns the CPU into an interrupt-driven machine with zero Thread-mode code after initialisation: `main()` sets things up, sets SLEEPONEXIT, and executes `WFI` once. Every subsequent wake-up runs only the ISR, then goes back to sleep without unstacking. That saves the 10–20 cycles of exception return and re-entry per event, plus the energy of fetching and running the main loop.

## 7.2 STM32L5 low-power modes

The vendor maps SLEEPDEEP plus `PWR_CR1.LPMS` onto these modes (RM0438, "Low-power modes"):

| Mode | Entry | Core | Clocks | SRAM / registers | Wake-up sources | Wake-up time (order of magnitude) | Typical current (order of magnitude) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Run (range 2, 16 MHz) | — | running | all | retained | — | — | ~100 µA/MHz |
| Sleep | WFI, SLEEPDEEP = 0 | stopped | peripherals running | retained | any interrupt | a few cycles | ~30–40% of Run |
| Low-power run | `PWR_CR1.LPR` | running ≤ 2 MHz | MSI | retained | — | — | tens of µA |
| Stop 0 | LPMS = 000 | stopped | HSI/HSE/PLL off; LSE/LSI on | retained, main regulator on | EXTI, RTC, LPTIM, LPUART, I2C (address match), USART (with HSI wake), COMP | ~2 µs | ~100 µA |
| Stop 1 | LPMS = 001 | stopped | as Stop 0, low-power regulator | retained | as Stop 0 | ~5 µs | ~10 µA |
| **Stop 2** | LPMS = 010 | stopped | as Stop 1; fewer peripherals powered | retained | EXTI, RTC, LPTIM1, LPUART1, I2C3, COMP | ~5–10 µs | **~2–5 µA** with RTC |
| Standby | LPMS = 011 | off | LSE/LSI only | **lost** (SRAM2 optionally kept with `PWR_CR3.RRS`); backup registers kept | WKUP pins, RTC, tamper, NRST, IWDG | reset (~tens of µs plus boot) | ~0.2–0.5 µA (+ SRAM2 retention) |
| Shutdown | LPMS = 1xx | off | LSE only | lost (backup domain kept) | WKUP pins, RTC, NRST | reset (BOR re-arm, longer) | tens of nA |

Take the exact figures from the STM32L552 **datasheet** (DS13014), "Electrical characteristics → Supply current". They depend on temperature, VDD and what's enabled. The column here is for orders of magnitude only.

What to take from the table:

- **Stop 2 is the workhorse.** Everything is retained, and wake-up takes microseconds to a known clock: `RCC_CFGR.STOPWUCK` picks MSI or HSI16 on wake. Code continues after the `WFI`.
- **Standby is a reset.** Execution starts again at `Reset_Handler`. Detect it with `PWR_SR1.SBF`, and keep state in SRAM2 (with RRS) or the RTC backup registers. It's worth it when the sleep interval is long and the wake-up work is short.
- **Only some peripherals work in Stop 2.** LPTIM1, LPUART1, I2C3 and the RTC, but not TIM2 or USART1. Choose the instance by power mode, not convenience.

## 7.3 Clock tree decisions

| Choice | Trade-off |
| --- | --- |
| MSI vs HSE vs HSI16 | MSI is cheapest and wakes fast. HSE is accurate but costs crystal drive current and start-up time (ms). HSI16 is a good wake-up clock for Stop. |
| Voltage range | Range 2 (≤ 26 MHz) uses noticeably less current per MHz than range 1/0. Drop the range whenever you drop the frequency. |
| PLL | Costs start-up time on every Stop wake-up, because it doesn't run in Stop. Avoid it in duty-cycled designs unless the work needs it. |
| **Race to sleep** vs slow and steady | Energy = P × t. A faster clock costs more power but less time. Static current (leakage, regulators) favours racing to sleep; dynamic current (∝ f) is roughly energy-neutral. Measure both on the real workload. |

## 7.4 Event-driven design

The low-power architecture is an inversion of the superloop:

```c
int main(void)
{
    init_everything();
    low_power_gpio();
    configure_wakeup_sources();
    SCB->SCR |= SCB_SCR_SLEEPONEXIT_Msk;   /* everything else happens in ISRs */
    for (;;) { enter_stop2(); }            /* reached only if SLEEPONEXIT is cleared */
}
```

Batch the work. Wake up once and do everything, instead of waking for each small job. On the STM32U5, **LPBAM** (low-power background autonomous mode) goes further: the LPDMA plus autonomous peripherals (LPUART, SPI3, I2C3, ADC4, LPTIM) can run whole acquisition sequences in Stop 2 with the CPU off. The CPU wakes only once a batch is complete.

## 7.5 Tickless idle with an RTOS

A 1 kHz SysTick wakes the CPU 1000 times a second. With 10 µs of work per wake at a few mA, that alone adds tens of µA to an average that should be single digits. **Tickless idle** removes it:

1. The idle task sees that the next task wake-up is N ticks away.
2. It stops SysTick and programs **LPTIM1**, clocked from LSE, which keeps running in Stop 2, to fire in N ticks.
3. It enters Stop 2.
4. On wake-up, either from the LPTIM or from another interrupt, it reads how much time actually passed, calls `vTaskStepTick(elapsed)`, and restarts SysTick.

FreeRTOS hooks:

```c
#define configUSE_TICKLESS_IDLE                 2   /* 2 = use our own implementation */
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   5   /* don't bother for shorter idles */
#define portSUPPRESS_TICKS_AND_SLEEP(xIdleTime) lp_suppress_ticks_and_sleep(xIdleTime)
```

```c
void lp_suppress_ticks_and_sleep(TickType_t expected)
{
    uint32_t lptim_counts = expected * LPTIM_COUNTS_PER_TICK;    /* LSE 32768 Hz / tick rate */
    if (lptim_counts > 0xFFFF) lptim_counts = 0xFFFF;

    __disable_irq();
    if (eTaskConfirmSleepModeStatus() == eAbortSleep) { __enable_irq(); return; }

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    lptim_start_oneshot(lptim_counts);
    enter_stop2();                                  /* WFI inside, PRIMASK still set */
    uint32_t elapsed_counts = lptim_elapsed();      /* stops the LPTIM */
    vTaskStepTick(elapsed_counts / LPTIM_COUNTS_PER_TICK);
    SysTick->VAL = 0;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    __enable_irq();                                 /* the waking ISR runs now */
}
```

The details that bite:

- **Tick drift.** The LPTIM counts in 1/32768 s steps and a tick is 1 ms, so rounding loses fractions. Carry the remainder over to the next sleep.
- **Wake-up by another interrupt.** `elapsed` is shorter than `expected`. Read the LPTIM counter; don't assume.
- **Clock after Stop.** The PLL is off, so restore it before restarting SysTick if your system clock uses it.

## 7.6 Energy budgeting

```math
\bar{I} = \frac{\sum_k I_k \, t_k}{T}
\qquad
\text{battery life} \approx \frac{C_{\text{usable}}}{\bar{I} + I_{\text{self-discharge}}}
```

Worked example: a CR2032 (≈ 220 mAh nominal; use ~180 mAh usable at low drain and room temperature), waking every 10 s:

| Phase | Current | Time per period | Charge (µC) |
| --- | --- | --- | --- |
| Wake-up + clock start | 1.5 mA | 0.05 ms | 0.08 |
| Sensor conversion (MCU in Stop 2) | 0.3 mA (sensor) + 3 µA | 2 ms | 0.61 |
| I2C read + filter (Run, 16 MHz) | 2.0 mA | 0.5 ms | 1.00 |
| Stop 2 with RTC | 3 µA | 9997.45 ms | 29.99 |
| **Total per 10 s** | | | **31.7 µC → 3.2 µA average** |

That's 180 mAh / 3.2 µA ≈ 56 000 h ≈ 6.4 years, before self-discharge (≈ 1%/year for lithium coin cells) and the cell's capacity loss at high pulse currents. **The sleep floor dominates.** Halving the active time barely matters; halving the Stop current nearly halves the average. That's why the leakage hunt below is worth an hour of your time.

## 7.7 The leakage hunt

Typical culprits when Stop 2 measures 50 µA instead of 3 µA:

| Culprit | Why | Fix |
| --- | --- | --- |
| **Floating inputs** | a floating CMOS input drifts to mid-rail, and both transistors conduct | every unused pin to **analog mode** (the reset state on L5, but code often changes it); used inputs get a pull resistor |
| Pull-ups fighting external pull-downs | constant current through two resistors | check the schematic for every pin with a pull |
| **Debug enabled in Stop** | `DBGMCU_CR.DBG_STOP` keeps clocks and regulators on so the debugger can stay connected | clear it in production; **disconnect the probe** when measuring (an attached ST-LINK also back-powers through SWD pins) |
| Peripheral clocks left on | the RCC gate still costs current in Stop for some peripherals | disable `RCCx_xxxENR` bits before sleep (and the `xxxSMENR` sleep-mode enables) |
| Board components | LEDs, the ST-LINK, level shifters, regulators' quiescent current | measure at the MCU's IDD jumper only, or on a custom board |
| VDDIO2 / USB / analog supplies enabled | `PWR_CR2.IOSV`, `USV`, VREFBUF | disable what isn't needed during sleep |

**Measurement setup.** Use a power profiler (Nordic PPK2, ST X-NUCLEO-LPM01A, Joulescope, Otii) in **ammeter mode** across the Nucleo's **IDD jumper**, which on the NUCLEO-L552ZE-Q measures only the MCU supply. Use the profiler's digital input to capture a GPIO that marks each firmware phase. A plain multimeter's averaging hides the pulses that dominate the energy budget, and its burden voltage at µA ranges can brown out a sleeping MCU.

## Knowledge check 7

1. What does setting SLEEPONEXIT in the SCR do?
2. A device draws 3 mA for 20 ms every 10 s and 2 µA otherwise. What is the approximate average current?
3. Why does leaving the debugger connected often inflate measured Stop-mode current?
4. What does Standby mode lose that Stop 2 retains?

## Further reading

- ST, *RM0438 STM32L552xx and STM32L562xx reference manual*, chapters "Power control (PWR)" and "Low-power modes".
- ST, *DS13014 STM32L552xx datasheet*, supply-current tables.
- ST, *AN4621 STM32L4 and STM32L4+ ultra-low-power features overview* (the L5 is close to the L4+).
- FreeRTOS, "Low Power Support / Tickless Idle Mode" (freertos.org).
- Nordic, *Power Profiler Kit II user guide*.
