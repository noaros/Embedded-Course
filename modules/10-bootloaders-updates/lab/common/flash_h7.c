#include "flash_h7.h"

#include "board.h"
#include "flash_layout.h"

#define FLASH_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR | \
                      FLASH_SR_OPERR)

static void unlock(void)
{
    if (FLASH->CR1 & FLASH_CR_LOCK) {
        FLASH->KEYR1 = 0x45670123u;
        FLASH->KEYR1 = 0xCDEF89ABu;
    }
}

static void lock(void)
{
    FLASH->CR1 |= FLASH_CR_LOCK;
}

static bool wait_idle(void)
{
    while (FLASH->SR1 & (FLASH_SR_QW | FLASH_SR_BSY)) {
    }
    uint32_t err = FLASH->SR1 & FLASH_ERRORS;
    FLASH->CCR1 = err | FLASH_CCR_CLR_EOP;
    return err == 0;
}

bool flash_erase_sector(unsigned sector)
{
    unlock();
    wait_idle();
    /* PSIZE = 11: 64-bit parallelism, the fastest setting; valid in VOS1. */
    FLASH->CR1 = (FLASH->CR1 & ~(FLASH_CR_SNB | FLASH_CR_PSIZE)) | FLASH_CR_SER |
                 (sector << FLASH_CR_SNB_Pos) | FLASH_CR_PSIZE_0 | FLASH_CR_PSIZE_1;
    FLASH->CR1 |= FLASH_CR_START;
    bool ok = wait_idle();
    FLASH->CR1 &= ~FLASH_CR_SER;
    lock();
    /* The D-cache may hold the old contents of the sector. */
    SCB_InvalidateDCache_by_Addr((void *)(BOOT_BASE + sector * SECTOR_BYTES), (int32_t)SECTOR_BYTES);
    return ok;
}

bool flash_program_word(uint32_t addr, const uint32_t data[8])
{
    if (addr % FLASH_WORD_SIZE) {
        return false;
    }
    unlock();
    wait_idle();
    FLASH->CR1 |= FLASH_CR_PG;
    volatile uint32_t *dst = (volatile uint32_t *)addr;
    /* Eight consecutive 32-bit writes fill the write buffer; the flash
     * programs the 256-bit word when it is complete. */
    for (int i = 0; i < 8; i++) {
        dst[i] = data[i];
    }
    __DSB();
    bool ok = wait_idle();
    FLASH->CR1 &= ~FLASH_CR_PG;
    lock();
    SCB_InvalidateDCache_by_Addr((void *)addr, FLASH_WORD_SIZE);
    return ok;
}
