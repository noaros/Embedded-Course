/* Minimal STM32H723 flash driver: sector erase and 32-byte flash-word programming. */
#ifndef FLASH_H7_H
#define FLASH_H7_H

#include <stdbool.h>
#include <stdint.h>

bool flash_erase_sector(unsigned sector);
/* Programs one 32-byte flash word. addr must be 32-byte aligned and erased. */
bool flash_program_word(uint32_t addr, const uint32_t data[8]);

#endif
