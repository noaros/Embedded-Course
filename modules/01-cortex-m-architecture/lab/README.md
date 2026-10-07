# Lab 1: Map the machine

**Board:** NUCLEO-H723ZG · **Time:** 2 h · **Builds on:** Module 1 §1.3–1.7

In this lab you measure the H723's memory system instead of trusting a table. You time the same 4 KB copy in three different RAMs, with the caches off and on. Then you try to make DMA1 write into DTCM and observe how it fails.

## What you hand in

1. A table of **cycles per byte** for each memory (DTCM, AXI SRAM, SRAM4) × cache state (off, on) × run (cold, warm), for both `memcpy` and the word-copy loop.
2. The DMA1 result for AXI → SRAM1 and AXI → DTCM: which flags were set, and how many bytes arrived.
3. One paragraph explaining the numbers. Cover why DTCM doesn't change with the cache, why SRAM4 is the slowest, and why the DTCM DMA fails.

## Files

```
starter/
  CMakeLists.txt
  main.c        measurement harness; you complete the TODOs
  dma_m2m.c/h   register-level DMA1 memory-to-memory helper (complete; Module 4 explains it)
```

Build from the repository root:

```sh
cmake -S . -B build -G Ninja      # or omit -G Ninja to use Make
cmake --build build --target lab01_starter
```

Flash `build/modules/01-cortex-m-architecture/lab/starter/lab01_starter.elf` (see [docs/setup.md](../../../docs/setup.md)) and open the virtual COM port at 115200 baud.

## Steps

### 1. Place the buffers (10 min)

`main.c` declares a source and a destination buffer for each memory. Check the map file (`lab01_starter.map`) and confirm the addresses:

- `dtcm_src`/`dtcm_dst` in `0x2000xxxx`
- `axi_src`/`axi_dst` in `0x2400xxxx`
- `sram4_src`/`sram4_dst` in `0x3800xxxx`

Write the six addresses in your report. If one is wrong, fix the placement attribute before you measure anything.

### 2. Implement `time_copy()` (20 min)

Complete `time_copy()` so it returns the cycles taken by one copy of `n` bytes, minus the cost of reading `DWT->CYCCNT` itself. Measure that overhead once at start-up in `measure_overhead()`.

Things that will bite you:

- The compiler can move the `CYCCNT` reads across the copy. Put `__DSB()` and a compiler barrier (`__asm volatile("" ::: "memory")`) on both sides.
- `memcpy` of a constant size may be inlined. The harness passes the size through a `volatile` to prevent that.

### 3. Measure (30 min)

Complete the loop in `main()`:

- For each cache state (both off, then I+D on), and for each memory:
  - Before the **cold** run with the D-cache on, clean and invalidate the whole D-cache (`SCB_CleanInvalidateDCache()`), so nothing is preloaded.
  - Record a cold run, then a **warm** run immediately after.
  - Do it for `memcpy` and for `copy_words()`.
- Print cycles/byte with two decimals. The harness prints fixed-point (cycles × 100 / bytes) because newlib-nano's `printf` has no `%f` by default.

### 4. DMA into DTCM (30 min)

Call `dma1_m2m_copy()` twice:

1. AXI SRAM → SRAM1. This should succeed. Check the data with `memcmp`.
2. AXI SRAM → DTCM. Record the returned `struct dma_result`: which flags are set and how many items were left in `NDTR`.

Remember that the CPU writes the source buffer through the D-cache. Clean it (`SCB_CleanDCache_by_Addr`) before starting the DMA, or the DMA reads stale RAM. Module 5 explains why. For now, just do it.

### 5. Explain (30 min)

Write your paragraph. Use the bus diagram in Module 1 §1.4 and RM0468's "System architecture" chapter.

## Stretch goals

- Repeat with a **copy from flash** (a `const` array) into each memory. What do flash wait states cost with the I-cache off?
- Place `copy_words()` in ITCM with `ITCM_FUNC` and measure again with the I-cache off.
- Raise the clock to 520 MHz (VOS0) and check which numbers scale with the clock and which don't.

## Grading

| Criterion | Points |
| --- | --- |
| All buffer addresses verified from the map file | 2 |
| Overhead-corrected, barrier-protected timing | 3 |
| Complete measurement table (2 copy methods × 3 memories × 2 cache states × cold/warm) | 3 |
| DMA results with flags and NDTR | 2 |
| Explanation is correct and cites the bus architecture | 4 |
| **Total** | **14** |
