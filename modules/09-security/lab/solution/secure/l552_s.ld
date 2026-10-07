/*
 * Lab 9: Secure image for STM32L552 with TZEN = 1.
 *
 *   0x0C000000 - 0x0C03DFFF  Secure code (bank 1, secure via SECWM1)
 *   0x0C03E000 - 0x0C03FFFF  Non-Secure Callable: the SG veneers only
 *   0x08040000 - 0x0807FFFF  Non-secure image (bank 2)
 *   0x30000000 - 0x3001FFFF  Secure SRAM (SRAM1, first 128 KB)
 *   0x20020000 - 0x2003FFFF  Non-secure SRAM (rest of SRAM1 + SRAM2)
 */

ENTRY(Reset_Handler)

_stack_size = 4K;
_heap_size  = 0;

MEMORY
{
  FLASH_S (rx) : ORIGIN = 0x0C000000, LENGTH = 248K
  NSC     (rx) : ORIGIN = 0x0C03E000, LENGTH = 8K
  RAM_S   (rw) : ORIGIN = 0x30000000, LENGTH = 128K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(512); KEEP(*(.isr_vector)) } > FLASH_S
  .text : { . = ALIGN(4); *(.text .text.*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH_S
  .rodata : { . = ALIGN(4); *(.rodata .rodata.*) . = ALIGN(4); } > FLASH_S
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH_S
  .init_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .);
    PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .);
    PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH_S

  /* The veneers generated for cmse_nonsecure_entry functions. Nothing else
   * may be in the NSC region: any SG-looking byte pattern there would be
   * an extra entry point into the Secure world. */
  .gnu.sgstubs :
  {
    . = ALIGN(32);
    *(.gnu.sgstubs*)
    . = ALIGN(32);
  } > NSC

  .stack (NOLOAD) : { . = ALIGN(8); _sstack = .; . = . + _stack_size; . = ALIGN(8); _estack = .; } > RAM_S
  .data : { . = ALIGN(4); _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM_S AT> FLASH_S
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } > RAM_S
  .heap (NOLOAD) : { . = ALIGN(8); _sheap = .; . = . + _heap_size; _eheap = .; } > RAM_S
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
