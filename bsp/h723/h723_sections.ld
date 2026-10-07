/*
 * Output sections shared by every H723 image in the course. Expects the
 * regions ITCM, FLASH, DTCM, AXI, SRAM1, SRAM2, SRAM4 and the symbols
 * _stack_size and _heap_size.
 */
SECTIONS
{
  /* VTOR needs the table aligned to the next power of two above its size
   * (179 entries * 4 = 716 bytes -> 1024). The start of flash satisfies that. */
  .isr_vector :
  {
    . = ALIGN(1024);
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
    *(.glue_7) *(.glue_7t)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH
  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  /* Initialised data: runs from DTCM (VMA), stored in flash (LMA). */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT> FLASH
  _sidata = LOADADDR(.data);

  /* Functions marked ITCM_FUNC: run from ITCM, stored in flash. */
  .itcm_text :
  {
    . = ALIGN(4);
    _sitcm = .;
    *(.itcm_text .itcm_text.*)
    . = ALIGN(4);
    _eitcm = .;
  } > ITCM AT> FLASH
  _siitcm = LOADADDR(.itcm_text);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > DTCM

  /* Not touched by startup: survives a warm reset. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(8);
    *(.noinit .noinit.*)
    . = ALIGN(8);
  } > DTCM

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    _sheap = .;
    . = . + _heap_size;
    . = ALIGN(8);
    _eheap = .;
  } > DTCM

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    _sstack = .;
    . = . + _stack_size;
    . = ALIGN(8);
    _estack = .;
  } > DTCM

  /* Zero-initialised buffers in the other RAMs (startup clears them). */
  .axi_bss (NOLOAD) :
  {
    . = ALIGN(32);
    _saxi_bss = .;
    *(.axi_bss .axi_bss.*)
    . = ALIGN(32);
    _eaxi_bss = .;
  } > AXI

  .sram1_bss (NOLOAD) :
  {
    . = ALIGN(32);
    _ssram1_bss = .;
    *(.sram1_bss .sram1_bss.*)
    . = ALIGN(32);
    _esram1_bss = .;
  } > SRAM1

  .sram2_bss (NOLOAD) :
  {
    . = ALIGN(32);
    _ssram2_bss = .;
    *(.sram2_bss .sram2_bss.*)
    . = ALIGN(32);
    _esram2_bss = .;
  } > SRAM2

  .sram4_bss (NOLOAD) :
  {
    . = ALIGN(32);
    _ssram4_bss = .;
    *(.sram4_bss .sram4_bss.*)
    . = ALIGN(32);
    _esram4_bss = .;
  } > SRAM4

  /* SRAM4, not touched by startup: survives any reset except power loss.
   * Used for crash records (Module 8). */
  .sram4_noinit (NOLOAD) :
  {
    . = ALIGN(8);
    *(.sram4_noinit .sram4_noinit.*)
    . = ALIGN(8);
  } > SRAM4

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
