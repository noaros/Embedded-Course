# Answer key and lab solution notes

**Instructor material: on the `solutions` branch only.** Reference code for every lab is in `modules/NN-*/lab/solution/`.

The "expected results" below describe what the reference solutions are designed to show. They haven't been measured on hardware yet. Treat numbers as orders of magnitude until you've run them on your boards, and replace them with your own measurements.

---

## Knowledge checks

### Module 1

1. `CONTROL.SPSEL` (bit 1): 0 = MSP, 1 = PSP. Ignored in Handler mode, which always uses MSP.
2. DTCM is attached only to the core (and, through the AHBS slave port, to MDMA). DMA1/DMA2 sit in the D2 domain with no bus path to it, so the access returns a bus error and the stream stops with TEIF set.
3. Any two of: TrustZone-M (optional), stack limit registers (MSPLIM/PSPLIM), the PMSAv8 base/limit MPU, the SG and secure-state instructions, banked SysTick/MPU/VTOR per security state.
4. False: bit-banding was removed in the Cortex-M7 and in all of ARMv8-M.

### Module 2

1. The initial MSP from offset 0x00 and the `Reset_Handler` address (Thumb bit set) from 0x04 of the vector table at the boot address.
2. The VMA is the run-time address in RAM, the one the code uses. The LMA is where the initial values are stored in flash. Startup copies LMA → VMA.
3. With CP10/CP11 disabled, any FP instruction raises a NOCP UsageFault. With `-mfloat-abi=hard` the compiler may emit FP instructions (or use FP registers as scratch) in any function, including the startup code itself.
4. `--gc-sections` discarding sections nothing references, such as the vector table, a firmware header or `.init_array`, which only hardware or the C runtime uses.

### Module 3

1. 4 preemption levels (2 bits), each with 4 sub-priorities.
2. When another exception is pending at exception return, the core goes straight to the next handler without unstacking and restacking: about 6 cycles instead of roughly 22–24 on M3/M4.
3. BASEPRI masks only priorities at or below a threshold, so time-critical ISRs above it never see the kernel's critical sections. PRIMASK masks everything.
4. The flag-clearing write is still in the write buffer or crossing bus bridges when the exception returns, so the NVIC samples the request as still asserted and re-pends it. Clear the flag early in the ISR, or read the register back before returning. On the H7, a DSB alone isn't enough.

### Module 4

1. The CPU processes the first half of the buffer while the DMA fills the second (and vice versa): continuous streaming with no gap.
2. Both masters arbitrate for the same bus-matrix slave and stall each other. Place DMA buffers in a different SRAM bank from the CPU's hot data and stack (on the H7: CPU data in DTCM, DMA buffers in D2 SRAM).
3. The IDLE flag fires when the line goes quiet after a burst. The number of bytes received is then the ring size minus the DMA's NDTR, so variable-length messages need no per-byte interrupt and no fixed length.
4. BDMA (with DMAMUX2).

### Module 5

1. No. `counter++` is still a separate load, add and store, and an interrupt can intervene between them. Use an atomic (LDREX/STREX, `<stdatomic.h>`) or a critical section.
2. Invalidate, after the transfer completes. Also make sure no dirty lines for the buffer exist before it starts (invalidate or clean+invalidate before).
3. Cache maintenance works on whole 32-byte lines. A line shared with other data can either overwrite DMA data (when a dirty neighbour is evicted) or lose the neighbour's update (when invalidated).
4. DSB followed by ISB.

### Module 6

1. So the context switch runs only after every other active ISR has completed, never preempting one, and so that several switch requests coalesce into one.
2. Yes. Total utilisation 0.75 is below the 3-task bound of 3(2^(1/3) − 1) ≈ 0.780.
3. It temporarily raises the low-priority holder to the blocked task's priority until it releases the mutex, so medium-priority tasks can't keep it off the CPU.
4. The task-level API may try to block, may request a context switch at the wrong time, and uses the task-level critical section. Leaving that from an ISR sets BASEPRI to 0 and unmasks whatever kernel critical section the ISR interrupted, so kernel lists get corrupted.

### Module 7

1. On exit from the last active ISR, the core goes straight back to sleep instead of returning to Thread mode.
2. About 8 µA: (3 mA × 0.02 s + 0.002 mA × 9.98 s) / 10 s ≈ (60 + 20) µC / 10 s.
3. The DBGMCU low-power debug bits keep clocks and regulators running so the probe stays connected, and the probe can source current into the target through the SWD pins.
4. Standby loses SRAM (except SRAM2 if retention is enabled with `PWR_CR3.RRS`) and peripheral and core register state, so wake-up is a reset. Stop 2 retains all SRAM and registers, and execution continues after the WFI.

### Module 8

1. Bit 2 of EXC_RETURN in LR: 0 means the frame is on MSP, 1 means PSP.
2. The bus reported the error after a buffered write had completed, by which time the core had moved on. The stacked PC is later than the faulting store, and BFAR isn't valid.
3. Any two of: it doesn't halt the CPU; it's very fast; it's bidirectional; it works without semihosting support, and doesn't hang (in non-blocking mode) when no debugger is attached.
4. MMFAR, valid when MMARVALID (CFSR bit 7) is set.

### Module 9

1. It holds the SG veneers, the only legal entry points from Non-secure code into the Secure world.
2. ARMv7-M uses power-of-two-sized, size-aligned regions with 8 sub-regions and higher-numbered regions overriding lower ones. ARMv8-M uses base/limit pairs at 32-byte granularity, forbids overlapping regions, and takes memory attributes indirectly through MAIR indexes.
3. It permanently disables the debug port and the system bootloader, and freezes the option bytes. No regression path exists, not even for ST.
4. It stops an attacker installing an older, validly signed image with known vulnerabilities.

### Module 10

1. Any four of: verify the image (signature, version and counter); disable interrupts and clear pending IRQs; stop SysTick and de-initialise the peripherals it used; set VTOR; load MSP from the app's table; branch to the app's reset handler.
2. A/B direct-execute is fast and low-wear, but needs images linked for each slot (or position-independent code). Swap-with-scratch keeps one link address, but costs update time, flash wear (especially on the scratch sector), a scratch area and complex power-fail logic.
3. A CRC only detects accidental corruption. Anyone can compute a valid CRC for a malicious image, but only the holder of the private key can produce a valid signature.
4. If the new image hangs before confirming, the watchdog resets into the bootloader, which counts the failed attempt and eventually reverts.

### Module 11

1. Any two of: link-time substitution, a function-pointer interface struct, weak symbols, preprocessor selection, register-struct fakes.
2. They detect undefined behaviour, out-of-bounds accesses and use-after-free, which silently corrupt memory on a target with no protection and surface far from the cause.
3. A documented, justified departure from a MISRA rule. It needs the rule, scope, rationale (why the risk doesn't apply, or how it's mitigated), and approval. Mandatory rules can't be deviated.
4. Any of: real timing and latency, interrupt interaction, DMA/cache behaviour, peripheral behaviour and silicon errata, power effects.

---

## Lab notes

### Lab 1: Map the machine

- **Expected pattern**: DTCM is fastest, and is unchanged by the D-cache (TCMs aren't cached). AXI SRAM is slow with caches off and close to DTCM when warm with caches on. SRAM4 is slowest, crossing D1→D2→D3 bridges. newlib-nano's `memcpy` is size-optimised (a byte loop), so `copy_words` usually beats it by ~3–4×.
- **DMA**: AXI → SRAM1 completes (TC=1, NDTR=0, data OK). AXI → DTCM gives TE=1, TC=0, NDTR ≈ the full count, and the destination unchanged.
- Common mistakes: no barriers around CYCCNT reads; forgetting to clean the source buffer before the DMA.

### Lab 2: Bare-metal from zero

- The solution puts the **stack at the bottom of DTCM**, so overflow runs into the reserved space below 0x20000000 and faults. Accept either placement if it's justified correctly.
- The `.fw_header` stretch goal is at 0x08000400. `objdump -s -j .fw_header` shows "DHWF" (the magic as bytes), version, size and the build-ID note.
- FPU experiment: CFSR = 0x00080000 (NOCP, bit 19), and HFSR.FORCED is set unless UsageFault is enabled.

### Lab 3: Latency budget

- Minimum EXTI latency is bounded below by GPIO input synchronisation (2 GPIO-clock cycles) + EXTI edge detection + NVIC + 12-cycle stacking + vector fetch: roughly 100–150 ns at 400 MHz.
- All-equal priorities: the EXTI and CONTROL maxima reach ~15 µs (the noise ISR) and ~10 µs (the PRIMASK section).
- Reworked: CONTROL at priority 0, with BASEPRI critical sections at threshold 2. The ITCM handlers and DTCM vector table remove cache-miss variance. Expected CONTROL jitter is tens of ns.

### Lab 4: 2 MSPS data logger

- Link budget: 136-byte frames every 512 µs = 266 kB/s, against ~303 kB/s at 3.03 Mbaud (BRR = 33). The 12% slack absorbs the status text, but only with the TX queue (4 frames).
- CPU load: the FIR is 64 outputs × 32 taps per 512 µs, so expect low single-digit percent at 400 MHz. Verify the load figure with an idle-loop counter.
- Minimum `HALF_SAMPLES` = 2 MSPS × (processing + worst-case latency). With ~20 µs processing, a half of 64 samples would technically work. 1024 gives > 10× margin for flash-erase stalls and printf.
- Items flagged "verify against RM0468": `EXTSEL` 13 = `tim6_trgo`, and the ADC BOOST setting for 50 MHz.

### Lab 5: Break it, then fix it

| Bug | Evidence | Mechanism | Fix |
| --- | --- | --- | --- |
| 1: rx shares a cache line with `tx`/stats | `tail` errors (bytes 96–99), counter anomalies once bug 2 is fixed | the TX-complete ISR dirties the line holding rx[96..99] during the DMA; it's evicted or read stale, and invalidating it discards the counter update | 32-byte-aligned, 32-byte-padded DMA buffers; bookkeeping elsewhere |
| 2: invalidate only before the DMA | rare `head` errors (about 1 in 8192 frames, when the trace peek runs) | the peek of rx[0] during the transfer caches a stale line | invalidate again after completion |
| 3: `done` not volatile | the LTO build times out on every frame; the disassembly shows no reload of `done` in the loop | with `app_idle_hook()` inlined, the compiler proves nothing in the loop writes `done` | `volatile` (or a C11 atomic with acquire/release) |

MPU variant: the same throughput on the wire, more CPU time per access to the buffer. Ship cache maintenance for buffers the CPU processes heavily, and non-cacheable memory for descriptors and small control blocks.

### Lab 6a: Mini kernel

- The PendSV path is ~30 instructions. Expected switch time (yield → other task running) is on the order of 100–200 cycles at 400 MHz, well under 1 µs.
- With inheritance, `hi`'s worst wait is ≤ about 20 ms (one `lo` critical section). Without it, ~100 ms+ (`mid`'s hog).

### Lab 6b: Broken FreeRTOS app

1. **Priority inversion**: binary semaphore → `xSemaphoreCreateMutex()`. The trace shows `control` blocked while `logger` runs during `sensor`'s bus hold.
2. **Stack overflow**: `logger` on 128 words with a 320-byte buffer + snprintf. The empty hook hid it. Give it 384 words, measure the high-water mark, and make the hook loud.
3. **`xQueueSend` from TIM6's ISR**: caught by `configASSERT` in `vPortEnterCritical` once `vAssertCalled` stops silently returning. Fix: `xQueueSendFromISR` + `portYIELD_FROM_ISR`.

### Lab 7: Coin-cell node

- The floor is the Stop 2 + RTC + LSE current from DS13014 (low single-digit µA). Wake phases contribute ~1–2 µC per period, giving ~3–4 µA average if leakage is under control.
- Typical leakage-hunt deltas: debug in Stop (tens to hundreds of µA), floating GPIO (µA–tens of µA), the probe connected (varies).
- `I2C1->TIMINGR = 0x00303D5B` (100 kHz at 16 MHz) is flagged: regenerate it with CubeMX for your exact clock.

### Lab 8: Crash detective

| Image | Report | Root cause |
| --- | --- | --- |
| 1 | UsageFault `INVSTATE`, PC = 0x00000000, LR in `case1_dispatch` | call through a NULL function pointer (BLX to an address with bit 0 clear) |
| 2 | MemManage `IACCVIOL`, PC = 0xA5A5A5A4; stack words full of 0xA5A5A5A5 | unbounded recursion grew the stack down over `.data`, overwriting `g_event_handler` with the scratch pattern; the call then jumps into execute-never space |
| 3 | BusFault `IMPRECISERR`, no BFAR; PC just after the stores in `case3_log_to_sdram` | write to 0xC0000000 (FMC SDRAM bank) with the FMC disabled; the buffered write errors later |
| 4 | UsageFault `UNALIGNED`, PC on the `ldrd` in `case4_read_u64` | LDRD on an odd address: needs word alignment even with `UNALIGN_TRP` = 0 |
| 5 | UsageFault `DIVBYZERO`, PC on the `sdiv` in `case5_average` | division by `g_samples_in_window` = 0 with `DIV_0_TRP` set |

With HardFault only: HFSR.FORCED is set in every case, and the CFSR bits still identify the class. After TODO 3 the specific handlers run (IPSR 4/5/6) and FORCED is clear.

M7 imprecise experiment: a `DSB` after each store moves the stacked PC to just after the DSB following the first faulting store. The fault is still flagged IMPRECISERR. `DISDEFWBUF` doesn't exist on the M7 (it's M3/M4 only).

### Lab 9: Partition and lock

- `s`: the MAC equals `hmac.new(bytes(range(0x10,0x30)), b'hello from the non-secure world', sha256)`, which is e12220469cc8a1f329aa07f5a86fddf6eedd7ea220c8fdc1ef89b0ee22b361e5 (verified on the host).
- `d` without the TODO 3 checks: returns 0, the key is overwritten, and the next `s` prints a different MAC. With the checks it returns −1.
- `k`: SecureFault with SFSR AUVIOL + SFARVALID, and SFAR = the key's address (0x3000xxxx).
- `b`: MemManage DACCVIOL with MMFAR in task B's guard, or MSTKERR if an exception entry hit the guard first.

### Lab 10: Unbrickable update

- (a) Appending the attempt before jumping means a hang or reset during the trial is already counted. Counting after a successful start would never count the boots that fail.
- (b) Only a confirmed image proves it works. Raising the floor on boot would let a broken release make the good previous image unbootable, and revert would then fail.
- (c) In `boot_state_append()`, between erasing the full log sector and programming the first new record. Fix: two sectors ping-pong, writing the new sector's first record before erasing the old one.
- The signing/verification path is cross-checked on the host: an image from `fw_sign.py` verifies with Monocypher, and a 1-bit payload change or an edited counter fails.

### Lab 11: Pipeline

- The reference tests reach 100% line coverage on `fir.c` and `frame.c` and 98% on `cmd_parser.c` under ASan + UBSan (gcov, measured).
- `-Wconversion` caught an implicit narrowing in the original Lab 4 CRC loop (since fixed in both labs). Use it as the "static analysis finding" example if a student has none.
