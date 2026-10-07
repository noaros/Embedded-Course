/*
 * Lab 2 reference solution: linker script for STM32H723ZG.
 *
 * Design choice: the stack is placed at the BOTTOM of DTCM (0x20000000
 * upwards). A stack overflow then runs below 0x20000000 into the reserved
 * gap under DTCM and raises a BusFault immediately, instead of silently
 * corrupting .data/.bss as it would if the stack sat above them.
 */

ENTRY(Reset_Handler)

_stack_size = 16K;
_heap_size  = 8K;

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
  .isr_vector ORIGIN(FLASH) :
  {
    KEEP(*(.isr_vector))
  } > FLASH
  ASSERT(SIZEOF(.isr_vector) <= 0x400, "vector table overlaps the firmware header")

  /* Stretch goal: firmware header at a fixed address, then the build ID. */
  .fw_header ORIGIN(FLASH) + 0x400 :
  {
    KEEP(*(.fw_header))
    . = ALIGN(4);
    __build_id_note = .;
    KEEP(*(.note.gnu.build-id))
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text .text.*)
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

  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  /* Stack first, at the bottom of DTCM. */
  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    _sstack = .;
    . = . + _stack_size;
    . = ALIGN(8);
    _estack = .;
  } > DTCM

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT> FLASH
  _sidata = LOADADDR(.data);

  .itcm_text :
  {
    . = ALIGN(4);
    _sitcm = .;
    *(.itcm_text .itcm_text.*)
    . = ALIGN(4);
    _eitcm = .;
  } > ITCM AT> FLASH
  _siitcm = LOADADDR(.itcm_text);

  /* Everything stored in flash ends here. */
  _image_end = LOADADDR(.itcm_text) + SIZEOF(.itcm_text);
  _image_size = _image_end - ORIGIN(FLASH);

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
    _eheap = .;
  } > DTCM

  .axi_bss (NOLOAD) :
  {
    . = ALIGN(32); _saxi_bss = .;
    *(.axi_bss .axi_bss.*)
    . = ALIGN(32); _eaxi_bss = .;
  } > AXI
  .sram1_bss (NOLOAD) :
  {
    . = ALIGN(32); _ssram1_bss = .;
    *(.sram1_bss .sram1_bss.*)
    . = ALIGN(32); _esram1_bss = .;
  } > SRAM1
  .sram2_bss (NOLOAD) :
  {
    . = ALIGN(32); _ssram2_bss = .;
    *(.sram2_bss .sram2_bss.*)
    . = ALIGN(32); _esram2_bss = .;
  } > SRAM2
  .sram4_bss (NOLOAD) :
  {
    . = ALIGN(32); _ssram4_bss = .;
    *(.sram4_bss .sram4_bss.*)
    . = ALIGN(32); _esram4_bss = .;
  } > SRAM4

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
