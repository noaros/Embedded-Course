/*
 * Lab 10 flash layout on the STM32H723ZG: 1 MB, single bank, 8 x 128 KB sectors.
 *
 *   sector 0     0x08000000  bootloader (128 KB)
 *   sectors 1-3  0x08020000  slot A (384 KB): 1 KB image header, then the application
 *   sectors 4-6  0x08080000  slot B (384 KB)
 *   sector 7     0x080E0000  boot state: append-only log of 32-byte records
 *
 * The H723 has no dual-bank flash and no bank swap (the H743/H753 and the
 * L5/U5 do), so A/B switching is done by the bootloader choosing which slot to
 * jump to, and each application is linked for the slot it will run from.
 */
#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#define SECTOR_BYTES 0x20000u
#define FLASH_WORD_SIZE   32u /* the H7 programs 256 bits at a time */

#define BOOT_BASE   0x08000000u
#define SLOT_A_BASE 0x08020000u
#define SLOT_B_BASE 0x08080000u
#define SLOT_SIZE   0x60000u
#define STATE_BASE  0x080E0000u
#define STATE_SIZE  SECTOR_BYTES

#define SLOT_A 0u
#define SLOT_B 1u
#define SLOT_BASE(s) ((s) == SLOT_A ? SLOT_A_BASE : SLOT_B_BASE)
#define SLOT_FIRST_SECTOR(s) ((s) == SLOT_A ? 1u : 4u)
#define SLOT_SECTORS 3u
#define STATE_SECTOR 7u

/* Shared between the bootloader and the application: the first (and only)
 * object in .sram4_noinit, so it has the same address in both images. */
#define UPDATE_REQUEST_MAGIC 0x55504454u /* "UPDT" */

#endif
