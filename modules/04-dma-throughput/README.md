# Module 4: DMA and high-throughput peripheral design

**Time:** 6 h (2.5 h theory, 3.5 h lab) · **Board:** NUCLEO-H723ZG · **Lab:** [2 MSPS data logger](lab/README.md)

The CPU should configure data movement, not perform it. A UART at 3 Mbaud with one interrupt per byte costs 300 000 interrupts a second. With DMA and idle-line detection it costs a few hundred. This module makes "use DMA" the default design instinct and gives you the tools to size, debug and recover DMA pipelines.

## Learning objectives

1. Configure STM32 DMA, DMAMUX, MDMA and BDMA at register level.
2. Build continuous streaming pipelines with circular mode, half-transfer/transfer-complete interrupts and double buffering.
3. Handle variable-length reception (UART idle line, receiver timeout) without per-byte interrupts.
4. Diagnose bus contention and FIFO errors, and recover a stalled stream without a reset.

---

## 4.1 The DMA landscape on STM32

| Controller | Where | Channels | Reaches | Request routing | Notable |
| --- | --- | --- | --- | --- | --- |
| **MDMA** | H7 D1 | 16 | everything incl. TCMs (via AHBS) | hardware triggers from DMA1/2 TC, peripherals in D1 | linked lists, block/repeated-block transfers, 64-bit AXI |
| **DMA1, DMA2** | H7 D2 | 8 streams each | AXI SRAM, SRAM1–3, SRAM4, all D2/D3 peripherals | DMAMUX1 | 16-byte FIFO, bursts, double-buffer mode |
| **BDMA** | H7 D3 | 8 channels | D3 only: SRAM4, LPUART1, SPI6, I2C4, ADC3… | DMAMUX2 | keeps running in Stop mode with D3 on |
| DMA1/2 | L5 | 8 channels each | SRAM1/2, flash, peripherals | DMAMUX1 | no FIFO, TrustZone-aware channels |
| **GPDMA1** | U5, H5 | 16 channels | everything | built-in request mux | linked-list items (LLI) in memory, 2D addressing on some channels |
| LPDMA1 | U5 | 4 channels | SRAM4, autonomous peripherals | | runs in Stop 2 with LPBAM |

**DMAMUX** decouples peripherals from streams. Any DMA1/2 stream can serve any request: you write the request ID into `DMAMUX1_Channelx->CCR.DMAREQ_ID`, where channel x maps to the stream (channels 0–7 → DMA1 streams 0–7, 8–15 → DMA2 streams 0–7). The request IDs are listed in RM0468's DMAMUX chapter ("DMAMUX1 request lines"). Examples for the H723:

| Request | ID |
| --- | --- |
| `adc1_dma` | 9 |
| `spi1_rx_dma` / `spi1_tx_dma` | 37 / 38 |
| `usart3_rx_dma` / `usart3_tx_dma` | 45 / 46 |

> Always check request IDs against your exact part's reference manual. They differ between STM32 families, and sometimes between lines in the same family.

## 4.2 Anatomy of a DMA1/2 stream

```
          ┌────────────── stream x ────────────────────┐
request ─►│ PAR ──► [FIFO 4 words] ──► M0AR / M1AR       │
(DMAMUX)  │ NDTR (items remaining)    CR: DIR, CIRC,     │
          │ FCR: DMDIS, FTH           PINC/MINC,         │
          │                           PSIZE/MSIZE, PBURST│
          │                           MBURST, PL, DBM, CT│
          └──────────────────────────────────────────────┘
flags in LISR/HISR: FEIF, DMEIF, TEIF, HTIF, TCIF  (clear via LIFCR/HIFCR)
```

| Field | Meaning |
| --- | --- |
| `DIR` | 00 periph→mem, 01 mem→periph, 10 mem→mem (`PAR` is then the *source*) |
| `PSIZE`/`MSIZE` | 8/16/32-bit item size on each side; the FIFO packs and unpacks between them |
| `PINC`/`MINC` | address increment per item |
| `NDTR` | number of **peripheral-size items**, max 65535 |
| `CIRC` | reload `NDTR` and addresses at the end and keep going |
| `DBM` + `CT` | double-buffer mode: alternate between `M0AR` and `M1AR`; `CT` says which one is current |
| `PL` | software priority, 0 (low) to 3 (very high); ties go to the lower stream number |
| `PBURST`/`MBURST` | single, INCR4, INCR8, INCR16; needs the FIFO and must fit its threshold |
| `FCR.DMDIS` | 1 = FIFO mode; 0 = direct mode (not allowed for mem→mem) |

**Configuration order** (the stream must be disabled, and `EN` must read back 0, before any field can change):

```c
s->CR &= ~DMA_SxCR_EN;
while (s->CR & DMA_SxCR_EN) { }          /* may take a while if a burst is in flight */
DMA1->LIFCR = all_flags_for_stream;       /* stale flags block re-enabling */
DMAMUX1_Channel1->CCR = 9;                /* request: adc1_dma */
s->PAR  = (uint32_t)&ADC1->DR;
s->M0AR = (uint32_t)buffer;
s->NDTR = N;
s->FCR  = 0;                              /* direct mode for a 16-bit periph→mem stream */
s->CR   = DMA_SxCR_MINC | DMA_SxCR_PSIZE_0 | DMA_SxCR_MSIZE_0
        | DMA_SxCR_CIRC | DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE
        | (2u << DMA_SxCR_PL_Pos);
s->CR  |= DMA_SxCR_EN;
```

Then enable the peripheral's own DMA request (`ADC_CFGR.DMNGT`, `USART_CR3.DMAR`/`DMAT`, `SPI_CFG1.RXDMAEN`) **last**, so the first request finds the stream ready.

## 4.3 Streaming: ping-pong buffers

Circular mode plus the half-transfer interrupt gives continuous streaming with one buffer:

```
buffer: [ half A | half B ]
DMA fills A ──► HT interrupt: CPU processes A while DMA fills B
DMA fills B ──► TC interrupt: CPU processes B while DMA fills A (wrapped)
```

The rule that makes it work: **the CPU must finish processing a half before the DMA comes back to it.** With `N` samples per half at rate `f`, the deadline is `N / f`.

**Sizing a buffer.** Take the worst case, not the average:

```
half_size ≥ f_sample × (t_process_worst + t_latency_worst)
```

where `t_latency_worst` is the longest time the HT/TC handler (or the task it wakes) can be delayed: higher-priority ISRs, critical sections, flash erase stalls. At 2 MSPS with 300 µs worst-case processing and 100 µs worst-case latency, each half needs ≥ 800 samples. Round up to a power of two (1024), which also helps cache alignment.

**Detecting a miss.** Make overruns visible instead of silent:

- Count HT/TC events in the ISR, and count halves processed in the consumer. If the difference exceeds 1, a half was overwritten before processing.
- Check the peripheral's own overrun flag (`ADC_ISR.OVR`, `USART_ISR.ORE`). DMA arbitration or bus contention can make the DMA itself too slow.
- In the output, put a **sequence number** on every block so the receiver can spot gaps.

**Double-buffer mode** (`DBM`) is the hardware version: two separate buffer addresses, and `CT` tells you which one the DMA is filling. Its advantage is that you can **change** the idle buffer's address on the fly (`M1AR` while `CT = 0`), which makes zero-copy buffer pools possible.

## 4.4 Variable-length reception

Per-byte RX interrupts don't scale. Three better tools:

1. **Idle-line detection.** `USART_CR1.IDLEIE` fires once the line has been idle for one character time after activity. Run the RX DMA in circular mode into a ring. In the IDLE handler, the write position is `size - NDTR`, and everything between the last position and this one is a complete burst.
2. **Receiver timeout.** `USART_RTOR` + `CR2.RTOEN` gives a configurable idle period, in bit times, for protocols with inter-byte gaps.
3. **Character match.** `CR2.ADD` + `CR1.CMIE` interrupts on a specific byte (for example `\n`).

```c
void USART3_IRQHandler(void)
{
    if (USART3->ISR & USART_ISR_IDLE) {
        USART3->ICR = USART_ICR_IDLECF;
        uint32_t pos = RX_SIZE - DMA1_Stream3->NDTR;   /* where DMA will write next */
        rx_consume(last_pos, pos);                      /* handles wrap-around */
        last_pos = pos;
    }
}
```

Also handle the HT and TC interrupts of the RX stream. A burst longer than half the ring would otherwise be overwritten before IDLE fires.

## 4.5 Timer-triggered DMA

Timers turn DMA into a precise waveform engine:

- **ADC at an exact rate.** The timer's TRGO (or a compare event) triggers each conversion, and DMA moves each result. No jitter from software, and the CPU is idle. Lab 4 does this.
- **DAC waveforms.** A timer update triggers a DMA request that writes the next sample to `DAC_DHR12R1`. A circular buffer gives a continuous wave.
- **Bit-banging via DMA.** A timer's update DMA request writes a table of `BSRR` values to a GPIO port, which gives arbitrary parallel patterns at up to tens of MHz. WS2812 LEDs are usually driven by the variant that writes PWM compare values (`CCRx`) from a table, one per bit.

## 4.6 Arbitration and bus contention

DMA throughput is limited by:

1. **Request rate**: the peripheral.
2. **Stream arbitration** inside a controller. One stream is served at a time, by `PL` priority and then by stream number. Bursts hold the bus for the whole burst.
3. **Bus matrix contention** at each slave. On the H7, the CPU's cache line fills, the DMA1/2 streams and MDMA all compete for the AXI SRAM and the D2 SRAMs.

Practical rules:

- **Separate the banks.** Put DMA buffers in a different RAM from the CPU's hot data. On the H7, the CPU's stack and globals go in DTCM (no contention at all), and DMA buffers go in SRAM1/2 or AXI SRAM.
- **Use bursts and the FIFO** for memory-side transfers. INCR4 bursts of 32-bit words cut arbitration overhead roughly fourfold.
- **Watch the APB.** A 100 MHz APB peripheral read through the bridges takes about 10 AHB cycles. Two high-rate streams on the same APB bus share it.
- Contention shows up as **peripheral overruns** (`ADC OVR`, `SPI OVR`, `USART ORE`), not as DMA errors. When you see those, think of the bus first.

## 4.7 Errors and recovery

| Flag | Cause | Effect |
| --- | --- | --- |
| `TEIF` | bus error: unreachable address (DTCM from DMA1!), unaligned, protected | stream disabled by hardware |
| `FEIF` | FIFO under/overrun, or a burst/size combination the FIFO can't hold | stream keeps running (unless direct mode), data may be wrong |
| `DMEIF` | direct mode: new request before the previous item was written | |
| peripheral overrun | DMA too slow (contention, priority) | data lost at the peripheral |

**Recovering a stream without a reset:**

1. Disable the peripheral's DMA request (`DMNGT = 0`, `DMAR = 0`).
2. Disable the stream and wait for `EN = 0`.
3. Clear **all** of its flags.
4. Re-program `NDTR`, `M0AR` and `PAR`, which may have been left mid-transfer.
5. Clear the peripheral's error state (`ADC_ISR.OVR` write-1-to-clear, then `ADSTART`; for a USART, `ICR.ORECF`).
6. Re-enable the stream, then the peripheral request.

Log every recovery with a counter. A system that "recovers silently" a thousand times a day has a design problem.

## 4.8 CPU load accounting

Measure the CPU time your pipeline really uses with the DWT cycle counter:

```c
uint32_t t0 = DWT->CYCCNT;
process_half(...);
busy_cycles += DWT->CYCCNT - t0;
/* every second: load% = busy_cycles * 100 / SystemCoreClock; busy_cycles = 0; */
```

Count ISR time too: time the HT/TC handler the same way. An idle loop that counts its own iterations, calibrated with interrupts off, is the classic alternative.

## Knowledge check 4

1. What does the half-transfer interrupt enable in a circular DMA design?
2. Why might DMA and the CPU accessing the same SRAM bank reduce throughput, and what is the fix?
3. How does UART idle-line detection help with variable-length packets?
4. On STM32H7, which DMA controller can reach SRAM4 in the D3 domain during low-power modes?

## Further reading

- ST, *RM0468*, chapters "DMA controller (DMA)", "DMA request multiplexer (DMAMUX)", "MDMA" and "BDMA".
- ST, *AN4031 Using the STM32F2, F4, F7 and H7 Series DMA controller*: FIFO, bursts, arbitration (written for F4 but applies to H7 DMA1/2).
- ST, *AN5593 How to use the GPDMA for STM32U5 Series*.
- ST, *AN4891* §"DMA" for measured concurrent throughput on the H7 bus matrix.
