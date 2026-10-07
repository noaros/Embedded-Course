/*
 * Lab 9: Non-secure image for STM32L552 with TZEN = 1.
 * Uses the course BSP startup (bsp/l552/startup_l552.c), so the section and
 * symbol names match bsp/l552/l552.ld.
 */

ENTRY(Reset_Handler)

_stack_size = 4K;
_heap_size  = 4K;

MEMORY
{
  FLASH_NS (rx) : ORIGIN = 0x08040000, LENGTH = 256K
  RAM_NS   (rw) : ORIGIN = 0x20020000, LENGTH = 128K
}

SECTIONS
{
  .isr_vector : { . = ALIGN(512); KEEP(*(.isr_vector)) } > FLASH_NS
  .text : { . = ALIGN(4); *(.text .text.*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH_NS
  .rodata : { . = ALIGN(4); *(.rodata .rodata.*) . = ALIGN(4); } > FLASH_NS
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH_NS
  .init_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .);
    PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .);
    PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH_NS

  .stack (NOLOAD) : { . = ALIGN(8); _sstack = .; . = . + _stack_size; . = ALIGN(8); _estack = .; } > RAM_NS
  .data : { . = ALIGN(4); _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM_NS AT> FLASH_NS
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } > RAM_NS
  .heap (NOLOAD) : { . = ALIGN(8); _sheap = .; . = . + _heap_size; _eheap = .; } > RAM_NS
  .noinit (NOLOAD) : { . = ALIGN(8); *(.noinit .noinit.*) } > RAM_NS
  .sram2_noinit (NOLOAD) : { . = ALIGN(8); *(.sram2_noinit .sram2_noinit.*) } > RAM_NS
  .ARM.attributes 0 : { *(.ARM.attributes) }
}
