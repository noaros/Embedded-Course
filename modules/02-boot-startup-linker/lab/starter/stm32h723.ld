/*
 * Lab 2: your linker script for STM32H723ZG.
 *
 * Rules: do not copy bsp/h723/h723.ld. Use RM0468's memory map and the
 * Module 2 notes. Work through TODO A..F, rebuilding after each.
 *
 * The placeholders at the bottom only exist so the starter links. Replace
 * each one with a real section as you go.
 */

ENTRY(Reset_Handler)

MEMORY
{
  ITCM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXI   (rw)  : ORIGIN = 0x24000000, LENGTH = 128K
  SRAM1 (rw)  : ORIGIN = 0x30000000, LENGTH = 16K
  SRAM2 (rw)  : ORIGIN = 0x30004000, LENGTH = 16K
  SRAM4 (rw)  : ORIGIN = 0x38000000, LENGTH = 16K
}

SECTIONS
{
  /* TODO A: the vector table must be first in flash, aligned for VTOR,
   * and must survive --gc-sections. */
  .isr_vector :
  {
    *(.isr_vector)
  } > FLASH

  .text :
  {
    *(.text .text.*)
    *(.rodata .rodata.*)
  } > FLASH

  /* TODO B: .data with VMA in DTCM and LMA in FLASH; export _sdata, _edata
   * and _sidata. Then .itcm_text the same way (VMA in ITCM). */

  /* TODO C: .bss in DTCM (NOLOAD) with _sbss/_ebss. */

  /* TODO D: .noinit in DTCM (NOLOAD), not touched by startup. */

  /* TODO E: .heap (export _sheap/_eheap, 8 KB) and .stack (16 KB, export
   * _estack at its top). Decide where the stack goes and justify it. */

  /* TODO F: the BSP puts buffers in .axi_bss, .sram1_bss, .sram2_bss and
   * .sram4_bss. Give each a NOLOAD section in the right memory. */

  /* --- placeholders: delete as you implement the sections above --- */
  _sheap  = ORIGIN(DTCM) + 64K;
  _eheap  = ORIGIN(DTCM) + 72K;
  _estack = ORIGIN(DTCM) + LENGTH(DTCM);
}
