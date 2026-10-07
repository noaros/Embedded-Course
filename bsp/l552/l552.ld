/*
 * Reference linker script for STM32L552ZE with TrustZone disabled.
 *
 *   FLASH  0x08000000 512 KB   2 banks x 128 pages x 2 KB (DBANK = 1)
 *   SRAM1  0x20000000 192 KB
 *   SRAM2  0x20030000  64 KB   parity-protected, can be retained in Standby
 *
 * The stack sits at the BOTTOM of SRAM1, below .data, so an overflow runs
 * off the start of RAM into unmapped space and faults immediately instead
 * of corrupting data. Compare with bsp/h723/h723.ld.
 */

ENTRY(Reset_Handler)

_stack_size = 8K;
_heap_size  = 4K;

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  SRAM1 (rw) : ORIGIN = 0x20000000, LENGTH = 192K
  SRAM2 (rw) : ORIGIN = 0x20030000, LENGTH = 64K
}

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(512);
    KEEP(*(.isr_vector))
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

  .ARM.extab : { *(.ARM.extab*) } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

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

  .stack (NOLOAD) :
  {
    . = ALIGN(8);
    _sstack = .;
    . = . + _stack_size;
    . = ALIGN(8);
    _estack = .;
  } > SRAM1

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data .data.*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM1 AT> FLASH
  _sidata = LOADADDR(.data);

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
  } > SRAM1

  .heap (NOLOAD) :
  {
    . = ALIGN(8);
    _sheap = .;
    . = . + _heap_size;
    _eheap = .;
  } > SRAM1

  .noinit (NOLOAD) :
  {
    . = ALIGN(8);
    *(.noinit .noinit.*)
  } > SRAM1

  .sram2_noinit (NOLOAD) :
  {
    . = ALIGN(8);
    *(.sram2_noinit .sram2_noinit.*)
  } > SRAM2

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
