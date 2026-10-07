/*
 * Reference linker script for STM32H723ZG (the course BSP).
 *
 * Memory map (RM0468, "Memory map and register boundary addresses"):
 *   ITCM   0x00000000  64 KB   CPU only, 0 wait states, instruction + data
 *   FLASH  0x08000000   1 MB   single bank, 8 x 128 KB sectors
 *   DTCM   0x20000000 128 KB   CPU and MDMA only, NOT reachable by DMA1/DMA2/BDMA
 *   AXI    0x24000000 128 KB   (320 KB with the default TCM_AXI_SHARED setting;
 *                               128 KB is used here so the script also fits
 *                               boards whose option bytes give more to ITCM)
 *   SRAM1  0x30000000  16 KB   D2 domain, reachable by DMA1/DMA2
 *   SRAM2  0x30004000  16 KB   D2 domain, reachable by DMA1/DMA2
 *   SRAM4  0x38000000  16 KB   D3 domain, reachable by BDMA, kept in Stop
 *
 * Layout choices:
 *   .data, .bss, heap and stack live in DTCM (fastest for the CPU).
 *   The stack sits ABOVE .bss and the heap, so an overflow silently corrupts
 *   them. Module 2 and Lab 8 look at why, and at how to move it.
 */

ENTRY(Reset_Handler)

_stack_size = 16K;
_heap_size  = 8K;

INCLUDE h723_memory.ld
INCLUDE h723_sections.ld
