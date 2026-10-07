#include "board.h"

/* MSI at 4 MHz out of reset; board_clock_init() updates this. */
uint32_t SystemCoreClock = 4000000u;

void board_clock_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;

    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }

    /* Range 2 (reset default): 16 MHz needs 1 wait state. Set it before the switch. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | FLASH_ACR_LATENCY_1WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_1WS) {
    }

    /* SW = 01: HSI16. STOPWUCK = 1: wake from Stop on HSI16, so the clock is
     * the same before and after a Stop-mode wakeup (Module 7). */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_0 | RCC_CFGR_STOPWUCK;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_0) {
    }

    SystemCoreClock = BOARD_SYSCLK_HZ;
}

void board_uart_init(uint32_t baud)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOGEN;
    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC->APB1ENR2;

    /* PG2..PG15 are supplied by VDDIO2; without IOSV they stay disconnected. */
    PWR->CR2 |= PWR_CR2_IOSV;

    /* LPUART1 kernel clock = HSI16 (LPUART1SEL = 10), so it keeps working
     * when SYSCLK changes. */
    RCC->CCIPR1 = (RCC->CCIPR1 & ~RCC_CCIPR1_LPUART1SEL) | RCC_CCIPR1_LPUART1SEL_1;

    gpio_set_af(GPIOG, 7, 8);
    gpio_set_af(GPIOG, 8, 8);

    BOARD_UART->CR1 = 0;
    /* LPUART: BRR = 256 * f_ck / baud. */
    BOARD_UART->BRR = (uint32_t)(((uint64_t)16000000u * 256u + baud / 2) / baud);
    BOARD_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void board_uart_putc(char c)
{
    while (!(BOARD_UART->ISR & USART_ISR_TXE_TXFNF)) {
    }
    BOARD_UART->TDR = (uint8_t)c;
}

int board_uart_getc(void)
{
    if (BOARD_UART->ISR & USART_ISR_ORE) {
        BOARD_UART->ICR = USART_ICR_ORECF;
    }
    if (!(BOARD_UART->ISR & USART_ISR_RXNE_RXFNE)) {
        return -1;
    }
    return (int)(BOARD_UART->RDR & 0xFFu);
}

void board_delay_ms(uint32_t ms)
{
    uint32_t per_ms = SystemCoreClock / 1000u;
    while (ms--) {
        dwt_delay_cycles(per_ms);
    }
}

void board_init(void)
{
    board_clock_init();
    dwt_init();

    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;
    gpio_set_mode(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_OUTPUT);
    gpio_set_mode(LED_BLUE_PORT, LED_BLUE_PIN, GPIO_OUTPUT);
    gpio_set_mode(LED_RED_PORT, LED_RED_PIN, GPIO_OUTPUT);
    gpio_set_mode(BUTTON_PORT, BUTTON_PIN, GPIO_INPUT);

    board_uart_init(115200);
}
