/*
 * STM32H723ZG memory regions (see h723.ld for the map and the reasoning).
 * Firmware that lives elsewhere in flash (the Lab 10 bootloader and
 * application slots) defines its own MEMORY and includes only
 * h723_sections.ld.
 */
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
