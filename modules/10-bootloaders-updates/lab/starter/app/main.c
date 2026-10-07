/*
 * Lab 10: the updatable application. Complete TODO 5-6.
 *
 * Linked for slot A or B (app_slot_X.ld); the bootloader verified it and
 * started the independent watchdog. On boot it prints its identity, runs a
 * short self-test, and, if the boot state says it is on trial, confirms
 * itself. Commands over USART3 (115200 baud):
 *   u  request an update: reboot into the bootloader's update mode
 *   s  print the boot state
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "boot_state.h"
#include "flash_layout.h"
#include "image.h"

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.0.0"
#endif
#ifndef APP_NEVER_CONFIRM
#define APP_NEVER_CONFIRM 0
#endif

/* Must be the only object in .sram4_noinit: same address as in the bootloader. */
SRAM4_NOINIT static volatile uint32_t g_update_request;

static const struct image_header *my_header(void)
{
    return (const struct image_header *)(SCB->VTOR - IMAGE_HEADER_SIZE);
}

static unsigned my_slot(void)
{
    return (SCB->VTOR - IMAGE_HEADER_SIZE) == SLOT_A_BASE ? SLOT_A : SLOT_B;
}

static void watchdog_refresh(void)
{
    /* TODO 5: refresh IWDG1 (KR = 0xAAAA). Until then, the app resets
     * every ~4 s once the bootloader starts the watchdog. */
}

/* A real self-test would check peripherals, sensors, the network link... */
static bool self_test(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_toggle(LED_YELLOW_PORT, LED_YELLOW_PIN);
        board_delay_ms(250);
        watchdog_refresh();
    }
    gpio_low(LED_YELLOW_PORT, LED_YELLOW_PIN);
    return true;
}

/* Confirm only once the self-test passed: from now on the bootloader keeps
 * booting this slot, and the anti-rollback floor rises to our counter. */
static void confirm_if_on_trial(void)
{
    /* TODO 6: if the boot state says this slot is PENDING, append a
     * CONFIRMED record with attempts = 0 and min_counter raised to this
     * image's security_counter (if higher). Respect APP_NEVER_CONFIRM, the
     * "bad release" build used to test reverting. */
    (void)my_header;
}

static void print_state(void)
{
    struct boot_record st;
    if (!boot_state_read(&st)) {
        printf("no boot state\n");
        return;
    }
    printf("state: seq %lu slot %c %s attempts %lu min_counter %lu\n", (unsigned long)st.seq,
           (int)('A' + st.slot), st.state == SLOT_CONFIRMED ? "CONFIRMED" : "PENDING",
           (unsigned long)st.attempts, (unsigned long)st.min_counter);
}

int main(void)
{
    board_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    watchdog_refresh();

    const struct image_header *h = my_header();
    printf("\nAPP v%s (header %lu.%lu.%lu) slot %c counter %lu\n", APP_VERSION_STR,
           (unsigned long)(h->version >> 16), (unsigned long)((h->version >> 8) & 0xFFu),
           (unsigned long)(h->version & 0xFFu), (int)('A' + my_slot()),
           (unsigned long)h->security_counter);

    if (self_test()) {
        confirm_if_on_trial();
    }

    for (;;) {
        watchdog_refresh();
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        for (int i = 0; i < 50; i++) {
            int c = board_uart_getc();
            if (c == 'u') {
                printf("APP: update requested, rebooting into the bootloader\n");
                g_update_request = UPDATE_REQUEST_MAGIC;
                board_delay_ms(10);
                NVIC_SystemReset();
            } else if (c == 's') {
                print_state();
            }
            board_delay_ms(10);
        }
    }
}
