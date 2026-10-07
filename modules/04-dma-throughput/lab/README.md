# Lab 4: 2 MSPS data logger

**Board:** NUCLEO-H723ZG · **Time:** 3.5 h · **Builds on:** Module 4 (Module 5 explains the cache calls you'll use here)

You build a pipeline that samples an ADC at 2 MSPS, filters and decimates on the CPU, and streams the result over a 3 Mbaud UART. There are no per-sample or per-byte interrupts, and dropped data is detected end to end.

```
TIM6 TRGO 2 MHz ─► ADC1 ─► DMA1 S1 (circular, HT/TC) ─► adc_buf[2 × 1024]  (SRAM1)
                                        │
                     main loop: FIR 32 taps, ÷16 ─► frame + seq + CRC
                                        │
                     TX queue ─► DMA1 S2 ─► USART3 TX 3 Mbaud ─► tools/lab4_rx.py
USART3 RX ─► DMA1 S3 (circular ring) ─► IDLE interrupt ─► "start" / "stop" / "stats"
```

## Hardware

- A signal on **PA3** (Arduino A0), 0–3.3 V. A function generator at a few kHz is ideal. Without one, use a potentiometer, or a jumper to a PWM pin through an RC filter.
- The ST-LINK V3 virtual COM port on this Nucleo supports 3 Mbaud.

## What you hand in

1. Your completed `main.c`.
2. A 1-hour run of `tools/lab4_rx.py` showing **0 gaps** and **0 CRC errors**, plus the logger's own status line with `missed=0 adc_ovr=0 tx_drop=0`.
3. **CPU load** at 2 MSPS, as reported by the logger, and how you would verify that the load figure itself is right.
4. A **buffer-sizing argument**: given your measured processing time per half, what is the smallest `HALF_SAMPLES` that still works, and what margin does 1024 give?
5. A **link budget** for the UART: bytes per second needed vs available at 3.03 Mbaud.

## Files

```
starter/
  main.c     pipeline with TODO 1–5
  fir.c/h    32-tap decimating FIR, Q15 (complete, host-testable; Lab 11 tests it)
  frame.c/h  frame format + CRC-16 (complete)
tools/lab4_rx.py   host receiver: checks CRC and sequence gaps, writes CSV
```

## Steps

### 1. ADC bring-up (TODO 1, 2), 60 min

Configure ADC1 at register level from RM0468's ADC chapter: power-up, calibration, the channel and the external trigger. The starter already runs PLL2 at 50 MHz as the ADC kernel clock (`pll2_init()`).

Check your trigger before adding DMA: temporarily enable `EOCIE`, count EOC interrupts for one second, and you should get 2 000 000 ± a few. Then turn it off again. That is exactly the per-sample interrupt design this lab exists to avoid.

> **Verify against the manual.** The trigger-source number for `tim6_trgo` (`ADC_EXTSEL_TIM6_TRGO`) and the BOOST setting depend on the exact device. Confirm both in RM0468 for your silicon revision.

### 2. Circular DMA with HT/TC (TODO 3, 4), 45 min

Configure DMA1 stream 1 and its interrupt handler. The main loop counts events and processes halves. Read the loop and understand how it detects a missed half.

Add a deliberate fault to prove the detection works: put `dwt_delay_cycles(SystemCoreClock / 1000)` (1 ms) in `process_half()`. `missed` must start counting. Remove it again.

### 3. UART TX via DMA (TODO 5a), 30 min

Complete `tx_start()`. The queue (`tx_enqueue()` and the stream-2 interrupt) is provided. Work out why the producer masks the stream's interrupt around the "is it idle?" check, and what goes wrong if it doesn't.

### 4. Idle-line RX (TODO 5b, 5c), 30 min

Configure the RX stream as a circular ring, and in the IDLE handler hand new bytes to `rx_consume()`. Test that commands work while streaming at full rate.

### 5. Run and measure (45 min)

```sh
pip install pyserial
python3 tools/lab4_rx.py /dev/ttyACM0 --seconds 3600
```

Then plot a few seconds of `--csv` output to check that the filter output looks like your input signal.

## Things that will go wrong

| Symptom | Likely cause |
| --- | --- |
| No HT/TC interrupts | DMAMUX request ID wrong, or `DMNGT` set before the stream was enabled |
| `TE=1` immediately | a buffer in DTCM (DMA1 can't reach it), or a wrong `PAR` |
| Filter output is stale or repeats | missing D-cache invalidate before reading a half |
| Host sees garbage | the TX buffer was not cleaned to RAM before the DMA started |
| `adc_ovr` counts | DMA stream priority too low, or bus contention |
| Commands ignored while streaming | IDLE position arithmetic wrong at the ring wrap |

## Stretch goals

- Move the FIR into ITCM and its coefficients and state into DTCM, then measure the load change.
- Replace the main-loop polling with `WFI` between halves, and measure the load difference with the idle counter technique.
- Use the stream's **double-buffer mode** (`DBM`, `M0AR`/`M1AR`) instead of HT/TC, and process directly from a pool of buffers.

## Grading

| Criterion | Points |
| --- | --- |
| ADC at exactly 2 MSPS from the timer trigger | 3 |
| Circular DMA with HT/TC, missed-half detection proven | 3 |
| TX DMA and idle-line RX working at full rate | 3 |
| 1-hour run with zero gaps, overruns and drops | 3 |
| CPU load, buffer sizing and link budget argued with numbers | 3 |
| **Total** | **15** |
