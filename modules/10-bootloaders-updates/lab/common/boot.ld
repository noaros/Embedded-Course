/* Lab 10 bootloader: sector 0 only. */
ENTRY(Reset_Handler)
_stack_size = 8K;
_heap_size  = 2K;

MEMORY
{
  ITCM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXI   (rw)  : ORIGIN = 0x24000000, LENGTH = 128K
  SRAM1 (rw)  : ORIGIN = 0x30000000, LENGTH = 16K
  SRAM2 (rw)  : ORIGIN = 0x30004000, LENGTH = 16K
  SRAM4 (rw)  : ORIGIN = 0x38000000, LENGTH = 16K
}

INCLUDE h723_sections.ld
