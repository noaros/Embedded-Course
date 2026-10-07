#include <stdio.h>

#include "board.h"

int main(void)
{
    board_init();
    printf("\n%s up, SYSCLK %lu Hz\n", BOARD_NAME, (unsigned long)SystemCoreClock);

    unsigned n = 0;
    for (;;) {
        gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        printf("tick %u, button %s\n", n++, gpio_read(BUTTON_PORT, BUTTON_PIN) ? "down" : "up");
        board_delay_ms(500);
    }
}
