# Lab 2: Bare-metal from zero

**Board:** NUCLEO-H723ZG · **Time:** 2.5 h · **Builds on:** Module 2

You replace the vendor startup file and linker script with your own, then prove they work. When you're done, you can point at any byte in the image and say why it's there.

## What you hand in

1. Your `startup.c` and `stm32h723.ld`.
2. Serial output showing `.data` OK, `.bss` OK, a boot count that rises on each B1 press and resets to 1 after a power cycle, and an ITCM address for `checksum_itcm`.
3. Map-file evidence (copied lines) for: the vector table's address and size, `.data`'s VMA and LMA, `.itcm_text`'s VMA and LMA, and the location of `.noinit`.
4. Two or three sentences on where you put the stack and why.

## Files

```
starter/
  CMakeLists.txt   builds with CUSTOM_STARTUP: the BSP startup file is NOT linked
  main.c           application that reports what your startup did (complete)
  startup.c        skeleton with TODO 1..7
  stm32h723.ld     skeleton with TODO A..F and placeholders that keep it linking
```

The starter **links but does not run correctly**. That's the point: each TODO fixes one visible symptom.

```sh
cmake --build build --target lab02_starter
```

## Rules

- Don't open `bsp/h723/startup_h723.c` or `bsp/h723/h723.ld` until you're finished. Use RM0468, the Module 2 notes and the GNU ld manual.
- You may `#include "vectors_h723.h"` (the generated IRQ list). Typing 163 handler names by hand teaches nothing.

## Steps

### 1. Vector table (TODO 1–3, TODO A), 30 min

Complete the system-exception part of the table, add the device IRQs, and make the section survive `--gc-sections` at the start of flash.

Check: `arm-none-eabi-objdump -h lab02_starter.elf` shows `.isr_vector` at `0x08000000` with size `0x2cc`. Disassemble the first two words:

```sh
arm-none-eabi-objdump -s -j .isr_vector lab02_starter.elf | head -3
```

The second word must be odd (Thumb bit set).

### 2. `.data`, `.bss` and the copy loops (TODO B, C, 4–6), 40 min

Write the sections, then the loops. Check the `.data` line in `objdump -h`: VMA `0x2000xxxx`, LMA `0x0800xxxx`. Run it, and `.data check` and `.bss check` should both report OK.

Debugger exercise: set a breakpoint on `main` **before** writing the copy loop and inspect `g_data_word`. What value does it hold, and why?

### 3. ITCM code (TODO B continued), 20 min

Add `.itcm_text` with VMA in ITCM and LMA in flash, plus its copy. `checksum_itcm` should print an address below `0x00010000`. Compare its cycle count with `checksum_flash`. The harness disables the caches for this measurement. Why?

### 4. `.noinit` and the boot counter (TODO D), 15 min

Press B1: the count goes up. Unplug and replug USB: it goes back to 1. Explain how `main.c` tells a valid record from power-on garbage.

### 5. Heap, stack and the other RAMs (TODO E, F), 20 min

Remove the placeholders. Decide whether the stack goes above or below `.data`/`.bss`, and write down the failure mode of each choice.

### 6. FPU and constructors (TODO 5, 7), 15 min

Experiment: comment out the CPACR write and add `volatile float f = 1.5f; f *= 2;` at the top of `main()`. Run it, and the core takes a UsageFault (NOCP) that escalates to HardFault. In the debugger, read `SCB->CFSR` and confirm bit 19 (NOCP) is set. Then restore the CPACR write.

## Stretch goals

- **Firmware header.** Put a `struct fw_header` (magic, version, image size, pointer to the build ID) at the fixed address `0x08000400`, and link with `-Wl,--build-id=sha1`. Read it back with `objdump -s -j .fw_header`.
- **Stack painting.** Paint the stack in `Reset_Handler` and print the high-water mark from `main()`.
- **`-fstack-usage`.** Add it to the target, and find the deepest function in the `.su` files.

## Grading

| Criterion | Points |
| --- | --- |
| Vector table correct (size, alignment, Thumb bit, KEEP) | 3 |
| `.data` / `.bss` / `.itcm_text` correct VMA/LMA and copy loops | 4 |
| Boot counter survives soft reset, resets on power cycle | 2 |
| Map-file evidence complete | 2 |
| Stack placement justified | 2 |
| FPU experiment explained (CFSR bit identified) | 1 |
| **Total** | **14** (+2 for the firmware-header stretch goal) |
