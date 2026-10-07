/* Lab 10 application linked for slot B: the 1 KB signed header comes first,
 * the vector table and code start at slot + 0x400. */
ENTRY(Reset_Handler)
_stack_size = 16K;
_heap_size  = 8K;

MEMORY
{
  ITCM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08080400, LENGTH = 384K - 1K
  DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXI   (rw)  : ORIGIN = 0x24000000, LENGTH = 128K
  SRAM1 (rw)  : ORIGIN = 0x30000000, LENGTH = 16K
  SRAM2 (rw)  : ORIGIN = 0x30004000, LENGTH = 16K
  SRAM4 (rw)  : ORIGIN = 0x38000000, LENGTH = 16K
}

INCLUDE h723_sections.ld
