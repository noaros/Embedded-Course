# Logging without printf costs

[← Module 8 wiki](README.md) · Lecture notes: [§8.2](../README.md#82-logging-without-printf-costs)

A log line exists to tell you what the system did. If the act of logging changes what the system does, by adding milliseconds inside an ISR, it can hide the very bug you're chasing (a "Heisenbug"). Choose the transport by how much it disturbs the system.

## The cost of a UART printf

At 115200 baud with 8N1 framing, each character is 10 bits on the wire: 10 / 115200 ≈ **87 µs**. A 40-character line takes about 3.5 ms. A blocking `printf` burns all of that, and on top of it `vsnprintf` formatting costs thousands of cycles and may use several hundred bytes of stack.

A buffered, DMA-driven UART removes the blocking, but the formatting cost stays. Once the buffer fills, you have to drop messages or block again.

## Comparison

| Method | Cost on target | Needs | Works without probe? |
| --- | --- | --- | --- |
| Blocking UART `printf` | ~87 µs/char + formatting | a UART pin | yes |
| [ITM over SWO](#itm-over-swo) | a few cycles per word, if the FIFO has room | SWO pin + a probe that captures SWO | yes (writes are dropped) |
| [SEGGER RTT](#segger-rtt) | a `memcpy` into RAM | a probe whose host software reads RTT | yes (in `NO_BLOCK_*` mode) |
| [Semihosting](#semihosting) | **halts the core**, ms per call | a debugger | **no**: it HardFaults |
| [Deferred binary logging](#deferred-binary-logging) | a few words per message | any byte transport | depends on the transport |

## ITM over SWO

The ITM has 32 **stimulus ports**. Writing a word to `ITM->PORT[n].u32` emits a trace packet tagged with `n`, which goes out through the TPIU on the SWO pin. CMSIS provides `ITM_SendChar()`, which uses port 0. Retarget `_write()` to it and `printf` goes out over SWO.

```c
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        ITM_SendChar(buf[i]);   /* returns immediately if ITM or port 0 is disabled */
    }
    return len;
}
```

Setup, which the debugger often does for you:

1. `DCB->DEMCR |= TRCENA`.
2. Unlock the ITM (`ITM->LAR = 0xC5ACCE55` on the M7). Enable it (`ITM->TCR |= ITMENA`, with a nonzero trace bus ID). Enable the ports you use (`ITM->TER`).
3. Set the SWO protocol (usually NRZ/UART) and set the prescaler so that the SWO bit rate equals what the probe expects: `SWO = trace clock / (prescaler + 1)`.
4. Configure the SWO pin as its alternate function (PB3 on the STM32H7).

**STM32H7 caveat:** on the H7 the SWO output passes through extra ST-specific trace components in the debug domain (a SWO unit and a trace funnel), and the trace clock must be enabled in `DBGMCU->CR`. The generic CMSIS `TPI->ACPR` setup alone isn't enough. STM32CubeIDE, Ozone and recent OpenOCD versions handle this. If you do it yourself, verify the register addresses against RM0468.

Costs and limits:

- A write takes a few cycles **if the ITM FIFO has room**. `ITM_SendChar()` *spins* while the FIFO is full, so at a low SWO rate a burst of text becomes blocking again. Raise the SWO speed (2–4 Mbit/s and up, depending on the probe) or log less.
- Output only: there's no way to send data to the target.
- With no probe connected, the ITM is usually disabled and the writes cost almost nothing.
- The ITM can add local **timestamps**, and DWT hardware packets (PC samples, exception trace) share the same pin.

## SEGGER RTT

RTT puts a **control block** in target RAM. It starts with the ID string `"SEGGER RTT"`, followed by descriptors for *up* buffers (target → host) and *down* buffers (host → target). Each buffer is a ring with read and write offsets. The target only ever writes its own side's offset, so no locking is needed between the target and the probe.

The probe finds the control block by scanning RAM for the ID, or you give it the address. It then reads and writes the rings over the DAP while the core runs.

```c
SEGGER_RTT_ConfigUpBuffer(0, NULL, NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
SEGGER_RTT_printf(0, "adc=%u\n", value);   /* or SEGGER_RTT_Write(0, buf, len) */
```

Why it's better than semihosting or UART:

- **No halt and no pins.** The target cost is a `memcpy`, plus formatting if you use `SEGGER_RTT_printf`.
- **Fast.** Throughput is limited by the probe's polling rate and SWD speed, typically tens of kB/s up to MB/s with a J-Link.
- **Bidirectional.** Down buffers give you a command shell.
- **Safe without a probe.** In `NO_BLOCK_SKIP` mode, a message that doesn't fit is dropped. In `NO_BLOCK_TRIM` mode it's truncated. Only `BLOCK_IF_FIFO_FULL` waits, so don't use that mode in a product.

Host support: J-Link (native), OpenOCD (`rtt setup` / `rtt start`), pyOCD and probe-rs. The ST-LINK firmware doesn't support RTT natively, but OpenOCD with an ST-LINK does.

**Cortex-M7 caveat:** the probe reads memory over the bus, not through the D-cache (see [CoreSight → DAP](coresight.md#dap-access-while-running)). Put the RTT control block and buffers in **DTCM** or a **non-cacheable** region. Otherwise the host sees stale write offsets, and data written by the host can be hidden behind stale cache lines.

## Semihosting

The target executes `BKPT 0xAB` with an operation code in R0 and a parameter block pointer in R1. The debugger halts, notices the special BKPT, performs the operation on the host (write to the console, open a file, read the clock), puts the result in R0 and resumes.

- Every call **halts the core** for a round trip over USB, which takes milliseconds. Interrupts stop being serviced, and timers keep counting.
- **With no debugger attached, the `BKPT` escalates to HardFault** with `HFSR.DEBUGEVT` set. A default infinite-loop handler makes this look like a hang. A firmware image built with `--specs=rdimon.specs` therefore won't run on a board out in the field.

Semihosting is useful for host-side file I/O in test rigs, such as dumping a buffer to a file on the PC. Never use it in timing-sensitive or shipped code.

## Deferred binary logging

Most of a log message is the constant format string, and the target doesn't need to send it. Instead:

```c
/* target: no formatting at all, just an ID and raw words */
#define LOG2(id, a, b) log_write3((id), (uint32_t)(a), (uint32_t)(b))
LOG2(LOG_ADC_OVERRUN, stream, ndtr);              /* 12 bytes on the wire */

/* host: a table built from the source at compile time maps
 * LOG_ADC_OVERRUN -> "ADC overrun on stream %u, NDTR=%u" */
```

The string `"ADC overrun on stream %u, NDTR=%u"` (33 bytes plus formatted digits) is never stored or formatted on the target. It's replaced by a 4-byte ID. Typical savings are **5–10× in bandwidth**, plus all of the formatting CPU time and much of the flash. The transport can be anything: ITM, RTT, UART or a flash ring buffer.

How the host gets the table:

- **Trice** (C) rewrites `TRICE(...)` calls in the source to embed IDs and keeps a `til.json` ID list.
- **defmt** (Rust) puts the strings in a non-loaded ELF section, and the host decoder reads them from the ELF.
- Zephyr's dictionary logging and Memfault's compact logs work along the same lines.

The catch: the decoder needs the **matching** table or ELF. A log captured from version 1.4.2 can't be decoded with the 1.4.3 table. That's the same rule as for crash dumps: [keep the exact ELF](post-mortem.md#keep-the-exact-elf).

## Choosing

- During development with a J-Link, or OpenOCD and any probe: **RTT**.
- With an ST-LINK and a free SWO pin, when you also want PC sampling or exception trace: **ITM/SWO**.
- In production, or for bandwidth-limited links: **deferred binary logging** to a RAM or flash ring, uploaded on request. It works well combined with the [crash record](crash-handler.md).
- Never use semihosting for logging. Never use a blocking UART inside an ISR.
