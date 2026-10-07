/*
 * Lab 2: Bare-metal from zero. Application side.
 *
 * You should not need to change this file much: the work is in startup.c
 * and stm32h723.ld. It prints what your startup code and linker script
 * produced, so you can check them against the map file.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"

#define BOOT_MAGIC 0xB007C0DEu

/* Survives a software or watchdog reset, but not a power cycle. */
struct boot_record {
    uint32_t magic;
    uint32_t count;
    uint32_t check; /* ~count, so a half-written or random value is detected */
};
NOINIT static struct boot_record g_boot;

/* Initialised data: proves your .data copy works. */
static uint32_t g_data_word = 0x12345678u;
static const char g_banner[] = "Lab 2: bare-metal from zero";

/* Zero-initialised data: proves your .bss zeroing works. */
static uint32_t g_bss_words[64];

/* The same function twice: once from ITCM, once from flash. */
#define CHECKSUM_BODY                                   \
    uint32_t sum = 0;                                   \
    for (size_t i = 0; i < n; i++) {                    \
        sum = (sum << 5) + sum + p[i];                  \
    }                                                   \
    return sum;

ITCM_FUNC uint32_t checksum_itcm(const uint8_t *p, size_t n) { CHECKSUM_BODY }
__attribute__((noinline)) uint32_t checksum_flash(const uint8_t *p, size_t n) { CHECKSUM_BODY }

static uint8_t g_work[8192];

static void update_boot_counter(void)
{
    if (g_boot.magic == BOOT_MAGIC && g_boot.check == ~g_boot.count) {
        g_boot.count++;
    } else {
        g_boot.magic = BOOT_MAGIC;
        g_boot.count = 1;
    }
    g_boot.check = ~g_boot.count;
}

static uint32_t time_checksum(uint32_t (*fn)(const uint8_t *, size_t), uint32_t *result)
{
    uint32_t t0 = DWT->CYCCNT;
    *result = fn(g_work, sizeof g_work);
    return DWT->CYCCNT - t0;
}

int main(void)
{
    board_init();
    update_boot_counter();

    printf("\n%s\n", g_banner);
    printf("boot count       : %lu (%s reset)\n", (unsigned long)g_boot.count,
           (RCC->RSR & RCC_RSR_SFTRSTF) ? "software" : "other");
    RCC->RSR |= RCC_RSR_RMVF; /* clear reset flags for next time */

    bool bss_ok = true;
    for (size_t i = 0; i < 64; i++) {
        bss_ok &= g_bss_words[i] == 0;
    }
    printf(".data check      : 0x%08lx (%s)\n", (unsigned long)g_data_word,
           g_data_word == 0x12345678u ? "OK" : "WRONG");
    printf(".bss check       : %s\n", bss_ok ? "OK" : "WRONG");
    printf("&g_data_word     : %p\n", (void *)&g_data_word);
    printf("&g_boot (.noinit): %p\n", (void *)&g_boot);
    printf("checksum_itcm    : %p\n", (void *)checksum_itcm);
    printf("checksum_flash   : %p\n", (void *)checksum_flash);

    for (size_t i = 0; i < sizeof g_work; i++) {
        g_work[i] = (uint8_t)(i ^ (i >> 8));
    }
    board_caches(false, false); /* make flash wait states visible */
    uint32_t r1, r2;
    uint32_t c_itcm = time_checksum(checksum_itcm, &r1);
    uint32_t c_flash = time_checksum(checksum_flash, &r2);
    board_caches(true, true);
    printf("checksum ITCM    : %lu cycles (0x%08lx)\n", (unsigned long)c_itcm, (unsigned long)r1);
    printf("checksum flash   : %lu cycles (0x%08lx)\n", (unsigned long)c_flash, (unsigned long)r2);

    printf("Press B1 for a software reset; the boot count should go up.\n");
    for (;;) {
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        board_delay_ms(250);
        if (gpio_read(BUTTON_PORT, BUTTON_PIN)) {
            board_delay_ms(50);
            NVIC_SystemReset();
        }
    }
}
