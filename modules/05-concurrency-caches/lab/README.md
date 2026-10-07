# Lab 5: Break it, then fix it

**Board:** NUCLEO-H723ZG · **Time:** 3 h · **Builds on:** Module 5

You're given an SPI + DMA driver that "passes testing" but corrupts frames once the D-cache is on, and gets worse in an optimised release build. There are **three** bugs. Find each one with evidence, explain its mechanism, fix it in place, and then re-implement the driver with an MPU non-cacheable region and compare.

## Hardware

Jumper **PA7** (MOSI, Arduino D11) to **PA6** (MISO, Arduino D12). If D11 on your board is routed to PB5 instead (check solder bridge SB in the Nucleo user manual), use PA7 on the morpho connector CN12.

## Files

```
starter/
  spi_dma.c/h   the driver under investigation (the bugs are here)
  main.c        soak-test harness: numbered frames, full compare, counter check
  CMakeLists.txt  builds lab05_starter (-O2) and lab05_starter_lto (-O2 -flto)
```

The harness prints every 10 000 frames:

```
10000 frames: bad 37 (head 1, tail 36), timeouts 0 | started 10000 tx_done 10000 rx_done 10000 | 562 kB/s, 97% in transfer
```

- **head/tail**: corruption in bytes 0–31 / 96–99, which tells you which cache lines are affected.
- **started / tx_done / rx_done**: the driver's own counters. They must stay equal.

## What you hand in

For each of the three bugs:

1. **Evidence**: the harness output (and/or debugger observation) that points to it.
2. **Mechanism**: what the compiler, core or cache does that makes it fail, in 3–5 sentences.
3. **Fix**: the diff.

Plus:

4. A throughput and CPU-time comparison: fixed driver (cache maintenance) vs MPU non-cacheable variant, at the same SPI clock.
5. One paragraph: which variant would you ship for this driver, and why?

## Steps

### 1. Baseline (15 min)

Flash `lab05_starter`. Let it run for at least 50 000 frames and record the numbers. Then flash `lab05_starter_lto` and record those. The two builds come from **identical source**.

### 2. Hunt (90 min)

Use what Module 5 taught you. A few prompts, so you're not guessing blindly:

- Look at the `tail` count. Which 32-byte line holds bytes 96–99 of `rx`, and what else lives in that line? (Get the addresses from the map file.)
- `head` errors are rare. What in the wait loop could bring a line of `rx` into the cache **while** the DMA is writing it?
- The LTO build times out on every frame. Disassemble the wait loop in both builds (`arm-none-eabi-objdump -d`) and compare how often `done` is loaded.

Fix each bug **in place**. Don't restructure the driver any more than the fix needs. Re-run the soak test after each fix and record how the numbers change. In particular, notice what happens to the counters when you fix bug 2 before bug 1.

### 3. The MPU alternative (45 min)

Make a second variant of the driver, selected with `-DSPI_USE_MPU_NOCACHE=1`:

- Put the DMA buffers in SRAM1 (`SRAM1_BSS`).
- Configure MPU region 0 to cover all 16 KB of SRAM1 as **Normal, non-cacheable**, shareable, execute-never. Use the CMSIS `ARM_MPU_*` helpers, and see Module 5 §5.7.
- Remove all cache maintenance from the driver.

Compare kB/s and "% in transfer" with your fixed cached variant. Then increase the harness's work on the received frame (for example, a CRC over `rx` 10 times) and compare again.

### 4. Write-up (30 min)

## Stretch goals

- Run the fixed driver for 1 000 000 frames with the SPI clock at 25 MHz (`MBR = 001`). Any errors now come from signal integrity, not software. How can you tell the difference from the harness output?
- Replace `volatile bool done` with a C11 `atomic_bool`, using `memory_order_release` in the ISR and `acquire` in the wait. Check the generated code.
- Use the M7's write-through attribute for the TX buffer region instead of non-cacheable. What maintenance is still needed?

## Grading

| Criterion | Points |
| --- | --- |
| Bug 1 (cache-line sharing): evidence, mechanism, fix | 3 |
| Bug 2 (invalidate only before DMA): evidence, mechanism, fix | 3 |
| Bug 3 (missing volatile, exposed by LTO): evidence incl. disassembly, fix | 3 |
| MPU non-cacheable variant working, zero errors | 3 |
| Measured comparison and shipping recommendation | 3 |
| **Total** | **15** |
