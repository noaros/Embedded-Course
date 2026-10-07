#include "board.h"

/* HSI is 64 MHz out of reset; board_clock_init() updates this. */
uint32_t SystemCoreClock = 64000000u;

void board_clock_init(void)
{
    /* 1. Power supply: the H723 has only the LDO. PWR_CR3 may be written once
     *    after reset; the write also confirms the supply configuration. */
    PWR->CR3 = (PWR->CR3 & ~PWR_CR3_BYPASS) | PWR_CR3_LDOEN;
    while (!(PWR->CSR1 & PWR_CSR1_ACTVOSRDY)) {
    }

    /* 2. Voltage scale 1 (VOS = 0b11) allows up to 400 MHz. */
    PWR->D3CR = (PWR->D3CR & ~PWR_D3CR_VOS) | (3u << PWR_D3CR_VOS_Pos);
    while (!(PWR->D3CR & PWR_D3CR_VOSRDY)) {
    }

    /* 3. HSE in bypass mode: the ST-LINK drives an 8 MHz clock into OSC_IN. */
    RCC->CR |= RCC_CR_HSEBYP | RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {
    }

    /* 4. PLL1: 8 MHz / 4 = 2 MHz reference (input range 2-4 MHz), x400 = 800 MHz
     *    VCO (wide range), P /2 = 400 MHz, Q /8 = 100 MHz, R /2 = 400 MHz. */
    RCC->PLLCKSELR = (RCC->PLLCKSELR & ~(RCC_PLLCKSELR_PLLSRC | RCC_PLLCKSELR_DIVM1)) |
                     RCC_PLLCKSELR_PLLSRC_HSE | (4u << RCC_PLLCKSELR_DIVM1_Pos);
    RCC->PLLCFGR = (RCC->PLLCFGR & ~(RCC_PLLCFGR_PLL1RGE | RCC_PLLCFGR_PLL1VCOSEL |
                                     RCC_PLLCFGR_PLL1FRACEN)) |
                   (1u << RCC_PLLCFGR_PLL1RGE_Pos) |
                   RCC_PLLCFGR_DIVP1EN | RCC_PLLCFGR_DIVQ1EN | RCC_PLLCFGR_DIVR1EN;
    RCC->PLL1DIVR = ((400u - 1) << RCC_PLL1DIVR_N1_Pos) | ((2u - 1) << RCC_PLL1DIVR_P1_Pos) |
                    ((8u - 1) << RCC_PLL1DIVR_Q1_Pos) | ((2u - 1) << RCC_PLL1DIVR_R1_Pos);
    RCC->CR |= RCC_CR_PLL1ON;
    while (!(RCC->CR & RCC_CR_PLL1RDY)) {
    }

    /* 5. Flash wait states BEFORE raising the clock. 200 MHz AXI clock in VOS1
     *    needs 2 WS; 3 WS leaves margin. Read back to make sure it took. */
    FLASH->ACR = FLASH_ACR_LATENCY_3WS | (2u << FLASH_ACR_WRHIGHFREQ_Pos);
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_3WS) {
    }

    /* 6. Bus prescalers: CPU /1, AXI/AHB /2, every APB /2. */
    RCC->D1CFGR = RCC_D1CFGR_D1CPRE_DIV1 | RCC_D1CFGR_HPRE_DIV2 | RCC_D1CFGR_D1PPRE_DIV2;
    RCC->D2CFGR = RCC_D2CFGR_D2PPRE1_DIV2 | RCC_D2CFGR_D2PPRE2_DIV2;
    RCC->D3CFGR = RCC_D3CFGR_D3PPRE_DIV2;

    /* 7. Switch SYSCLK to PLL1. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL1;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL1) {
    }

    SystemCoreClock = BOARD_SYSCLK_HZ;
}

void board_caches(bool icache, bool dcache)
{
    if (icache) {
        SCB_EnableICache();
    } else {
        SCB_DisableICache();
    }
    if (dcache) {
        SCB_EnableDCache();
    } else {
        SCB_DisableDCache(); /* cleans dirty lines before disabling */
    }
}

void board_uart_init(uint32_t baud)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIODEN;
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC->APB1LENR;

    gpio_set_af(GPIOD, 8, 7);
    gpio_set_af(GPIOD, 9, 7);

    BOARD_UART->CR1 = 0;
    /* USART3 kernel clock defaults to PCLK1, oversampling by 16. PCLK1 is
     * 100 MHz after board_clock_init(), or the 64 MHz HSI straight out of
     * reset (all prescalers 1), e.g. in the Lab 10 bootloader. */
    uint32_t pclk1 = (SystemCoreClock == BOARD_SYSCLK_HZ) ? BOARD_APB1_HZ : SystemCoreClock;
    BOARD_UART->BRR = (pclk1 + baud / 2) / baud;
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

static void leds_and_button_init(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIOEEN;
    (void)RCC->AHB4ENR;
    gpio_set_mode(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_OUTPUT);
    gpio_set_mode(LED_YELLOW_PORT, LED_YELLOW_PIN, GPIO_OUTPUT);
    gpio_set_mode(LED_RED_PORT, LED_RED_PIN, GPIO_OUTPUT);
    gpio_set_mode(BUTTON_PORT, BUTTON_PIN, GPIO_INPUT);
}

void board_init(void)
{
    board_clock_init();
    dwt_init();
    leds_and_button_init();
    board_uart_init(115200);
    board_caches(true, true);
}
