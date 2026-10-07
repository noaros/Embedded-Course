# Lab 8: Crash detective

**Board:** NUCLEO-H723ZG · **Time:** 3 h · **Builds on:** Module 8, Module 2 (`.noinit`), Module 3 (exception frames)

Five firmware images each crash in a different way, three seconds after boot. You build the crash-capture mechanism, then diagnose each crash **to file and line using only the report the firmware prints on its next boot**. The debugger may not be attached while the crash happens.

## Files

```
starter/
  main.c          the five crash cases (CRASH_CASE=1..5). Read it only AFTER diagnosing from the report.
  crash.h         the crash record layout
  crash.c         capture side: TODO 1-3 (naked handler, record, enabling fault handlers)
  crash_report.c  next-boot side: validates the CRC, decodes CFSR/HFSR, prints (complete)
  CMakeLists.txt  builds lab08_starter_case1 ... lab08_starter_case5
```

```sh
cmake --build build --target lab08_starter_case1   # ... case5
```

## What you hand in

For each of the five images:

1. The **crash report** as printed on the next boot.
2. Your **diagnosis**: fault class and CFSR bits, the faulting instruction (`addr2line` output plus the disassembly line), and the root cause in one or two sentences.
3. Which bits made the diagnosis possible, and what you would have concluded from `HFSR = FORCED` alone.

Plus:

4. The **imprecise-fault experiment** (step 4) with before/after reports.
5. One paragraph: what would you add to this crash record for a real product?

## Steps

### 1. Capture (TODO 1, 2), 60 min

- **TODO 1**: the naked `HardFault_Handler`. EXC_RETURN bit 2 selects MSP or PSP. Make the three configurable-fault vectors aliases of it.
- **TODO 2**: `fault_handler_c()`. Fill the record, guard every read of the frame with `in_ram()`, store the CRC, and reset.

Test with case 1. You should see the report on the second boot. If the board instead stops responding, your handler faulted inside the fault. Attach the debugger *after* that has happened and read `SCB->HFSR` and `DHCSR` (`S_LOCKUP`).

### 2. Diagnose all five, with HardFault only (45 min)

Leave `crash_init()` empty (all faults escalate to HardFault). Run all five images and diagnose each one from its report. Don't open `main.c`.

For each one, use `arm-none-eabi-addr2line -f -e <elf> <pc> <lr>` and `arm-none-eabi-objdump -d <elf>` around the PC.

Hints, if you're stuck:

- `INVSTATE` with `PC = 0`: what does a branch to address 0 with bit 0 clear try to do?
- `PC = 0xA5A5A5A4`: where would that value have come from? Look at the stack words in the record.
- `IMPRECISERR` without `BFARVALID`: the PC is *not* the culprit. Look at the instructions **before** it.

### 3. Enable the configurable fault handlers (TODO 3), 15 min

Set `MEMFAULTENA`, `BUSFAULTENA` and `USGFAULTENA` in `SCB->SHCSR`. Re-run all five. Which handler now runs for each case, and what happened to `HFSR.FORCED`?

### 4. Make the imprecise fault precise (30 min)

On a Cortex-M3/M4, you'd set `SCnSCB->ACTLR.DISDEFWBUF` to disable write buffering and make every bus fault precise. **The Cortex-M7 doesn't implement that bit.** Open `core_cm7.h` and compare its `ACTLR` definitions with `core_cm4.h`. On the M7:

1. In a **copy** of the imprecise case, add `__DSB()` after each of the two suspect stores.
2. Re-run. Compare the stacked PC with the original report. Is the fault still flagged `IMPRECISERR`? Does the PC now pin the faulting store?
3. Explain why the DSB changes where the fault is reported, and what it costs if you leave it in production code.

If you have an STM32F4/L4 board, do the `DISDEFWBUF` version too and compare.

### 5. Write-up (30 min)

## Stretch goals

- **Watchpoint without a debugger.** Use DWT comparator 0 and the DebugMonitor exception (`DCB->DEMCR.MON_EN`) to catch the first write to `g_event_handler` in case 2, and record the writing PC in the crash record.
- **Backtrace.** Scan the stack snapshot for values in the `.text` range (`__etext`, from the map file) and print them as candidate return addresses.
- **Build ID.** Add Lab 2's build-ID header to the record, so a report can always be matched with the right ELF.

## Grading

| Criterion | Points |
| --- | --- |
| Naked handler + record + CRC, robust against a bad SP | 4 |
| Five correct diagnoses to file and line, with CFSR evidence | 5 (1 each) |
| Configurable handlers enabled; FORCED explained | 2 |
| Imprecise-fault experiment done and explained (incl. the M7/M4 difference) | 3 |
| Product-recommendation paragraph | 1 |
| **Total** | **15** |
