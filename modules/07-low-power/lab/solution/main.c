/*
 * Lab 7: coin-cell sensor node. Reference solution.
 *
 * Every 10 s: wake from Stop 2 on the RTC wake-up timer, start an SHT4x
 * conversion over I2C1, sleep in Stop 2 for 2 ms while it converts, read
 * and check the result, filter it, store it, and go back to Stop 2.
 * Once a minute, print a summary over LPUART1.
 *
 * Pins: I2C1 SCL PB8 (D15), SDA PB9 (D14), AF4. Phase marker PA5 (D13): high
 * while awake, for the power profiler's digital input.
 *
 * Build options:
 *   LAB7_KEEP_DEBUG=1  keep SWD usable in Stop (inflates the current: see Module 7 §7.7)
 */
#include <stdio.h>
#include <string.h>

#include "board.h"

#ifndef LAB7_KEEP_DEBUG
#define LAB7_KEEP_DEBUG 0
#endif

#define PERIOD_S 10u
#define REPORT_EVERY 6u /* wakes: once a minute */
#define SHT4X_ADDR 0x44u
#define SHT4X_MEASURE_LOW_REP 0xE0u /* 1.6 ms max conversion */
#define LOG_LEN 64u

#define MARK_PORT GPIOA
#define MARK_PIN 5u

/* RTC wake-up clock selections (RTC_CR.WUCKSEL). */
#define WUCK_RTC_DIV2 3u /* 16384 Hz with LSE */
#define WUCK_CK_SPRE 4u  /* 1 Hz with the default prescalers */

struct sample {
    int16_t temp_c_x100;
    uint16_t rh_x100;
};

/* SRAM2: retained in every Stop mode and (with PWR_CR3.RRS) in Standby. */
SRAM2_DATA static struct {
    struct sample log[LOG_LEN];
    uint32_t head;
} g_log;

static volatile bool g_rtc_fired;
static uint32_t g_wakes, g_i2c_errors, g_crc_errors;
static int32_t g_filtered_x100; /* exponential moving average, x100 */
static bool g_have_filter;

/* ------------------------------------------------------------------ */
/* GPIO: everything analog unless we use it                            */
/* ------------------------------------------------------------------ */

static void lp_gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN |
                    RCC_AHB2ENR_GPIODEN | RCC_AHB2ENR_GPIOEEN | RCC_AHB2ENR_GPIOFEN |
                    RCC_AHB2ENR_GPIOGEN | RCC_AHB2ENR_GPIOHEN;
    (void)RCC->AHB2ENR;

    /* Analog mode (MODER = 11) disconnects the input Schmitt trigger, so a
     * floating pin draws nothing. */
    GPIO_TypeDef *const ports[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH};
    for (unsigned i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        uint32_t keep = 0;
        if (ports[i] == GPIOA) {
            keep = (1u << MARK_PIN);
#if LAB7_KEEP_DEBUG
            keep |= (1u << 13) | (1u << 14); /* SWDIO, SWCLK */
#endif
        } else if (ports[i] == GPIOB) {
            keep = (1u << 8) | (1u << 9); /* I2C1 */
        } else if (ports[i] == GPIOG) {
            keep = (1u << 7) | (1u << 8); /* LPUART1 */
        }
        uint32_t moder = 0xFFFFFFFFu;
        for (unsigned pin = 0; pin < 16; pin++) {
            if (keep & (1u << pin)) {
                moder = (moder & ~(3u << (2 * pin))) | (ports[i]->MODER & (3u << (2 * pin)));
            }
        }
        ports[i]->MODER = moder;
    }

    gpio_set_mode(MARK_PORT, MARK_PIN, GPIO_OUTPUT);
    gpio_high(MARK_PORT, MARK_PIN);

    /* Only A (marker), B (I2C) and G (UART) need clocks from now on. */
    RCC->AHB2ENR &= ~(RCC_AHB2ENR_GPIOCEN | RCC_AHB2ENR_GPIODEN | RCC_AHB2ENR_GPIOEEN |
                      RCC_AHB2ENR_GPIOFEN | RCC_AHB2ENR_GPIOHEN);
}

/* ------------------------------------------------------------------ */
/* RTC wake-up timer on LSE                                            */
/* ------------------------------------------------------------------ */

static void rtc_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
    (void)RCC->APB1ENR1;
    PWR->CR1 |= PWR_CR1_DBP; /* unlock the backup domain */

    if (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
        RCC->BDCR |= RCC_BDCR_LSEON; /* lowest drive strength (reset default) */
        while (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
        }
    }
    RCC->BDCR = (RCC->BDCR & ~RCC_BDCR_RTCSEL) | RCC_BDCR_RTCSEL_0 | RCC_BDCR_RTCEN; /* LSE */

    /* The RTC interrupt reaches the CPU, and wakes it from Stop, through
     * EXTI line 17 (see the RTC_IRQn comment in stm32l552xx.h). */
    EXTI->IMR1 |= EXTI_IMR1_IM17;
    NVIC_SetPriority(RTC_IRQn, 3);
    NVIC_EnableIRQ(RTC_IRQn);
}

/* Wake-up after (counts) periods of the selected clock. */
static void rtc_wakeup_in(uint32_t counts, uint32_t wucksel)
{
    RTC->WPR = 0xCA;
    RTC->WPR = 0x53;
    RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
    while (!(RTC->ICSR & RTC_ICSR_WUTWF)) {
    }
    RTC->WUTR = counts - 1u;
    RTC->CR = (RTC->CR & ~RTC_CR_WUCKSEL) | (wucksel << RTC_CR_WUCKSEL_Pos);
    RTC->SCR = RTC_SCR_CWUTF;
    RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
    RTC->WPR = 0xFF;
}

void RTC_IRQHandler(void)
{
    if (RTC->SR & RTC_SR_WUTF) {
        RTC->SCR = RTC_SCR_CWUTF;
        g_rtc_fired = true;
    }
}

/* ------------------------------------------------------------------ */
/* Stop 2                                                              */
/* ------------------------------------------------------------------ */

static void enter_stop2_until_rtc(void)
{
    gpio_low(MARK_PORT, MARK_PIN);

    /* LPUART1 must have finished sending before its clock stops. */
    while (!(LPUART1->ISR & USART_ISR_TC)) {
    }

    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_LPMS) | PWR_CR1_LPMS_1; /* 010: Stop 2 */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    /* Race-free: check the flag with interrupts masked; WFI still wakes on
     * the pending RTC interrupt, which runs once PRIMASK is cleared. */
    __disable_irq();
    while (!g_rtc_fired) {
        __DSB();
        __WFI();
        __enable_irq();
        __disable_irq();
    }
    g_rtc_fired = false;
    __enable_irq();

    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
    /* STOPWUCK = 1 (board_clock_init): we woke up on HSI16, as before. */
    gpio_high(MARK_PORT, MARK_PIN);
}

/* ------------------------------------------------------------------ */
/* I2C1 + SHT4x                                                        */
/* ------------------------------------------------------------------ */

static void i2c1_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    (void)RCC->APB1ENR1;
    for (unsigned pin = 8; pin <= 9; pin++) {
        gpio_set_open_drain(GPIOB, pin, true);
        gpio_set_af(GPIOB, pin, 4);
    }
    I2C1->CR1 = 0;
    /* 100 kHz from the 16 MHz PCLK. Generate this value for your clock with
     * STM32CubeMX's I2C timing tool (RM0438, "I2C timings"). */
    I2C1->TIMINGR = 0x00303D5Bu;
    I2C1->CR1 = I2C_CR1_PE;
}

static bool i2c_wait(uint32_t flag)
{
    uint32_t t0 = dwt_cycles();
    while (!(I2C1->ISR & flag)) {
        if (I2C1->ISR & I2C_ISR_NACKF) {
            I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
            return false;
        }
        if (dwt_cycles() - t0 > SystemCoreClock / 100u) {
            return false;
        }
    }
    return true;
}

static bool i2c_write(uint8_t addr, const uint8_t *data, size_t n)
{
    I2C1->CR2 = ((uint32_t)addr << 1) | ((uint32_t)n << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND |
                I2C_CR2_START;
    for (size_t i = 0; i < n; i++) {
        if (!i2c_wait(I2C_ISR_TXIS)) {
            return false;
        }
        I2C1->TXDR = data[i];
    }
    bool ok = i2c_wait(I2C_ISR_STOPF);
    I2C1->ICR = I2C_ICR_STOPCF;
    return ok;
}

static bool i2c_read(uint8_t addr, uint8_t *data, size_t n)
{
    I2C1->CR2 = ((uint32_t)addr << 1) | I2C_CR2_RD_WRN | ((uint32_t)n << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_AUTOEND | I2C_CR2_START;
    for (size_t i = 0; i < n; i++) {
        if (!i2c_wait(I2C_ISR_RXNE)) {
            return false;
        }
        data[i] = (uint8_t)I2C1->RXDR;
    }
    bool ok = i2c_wait(I2C_ISR_STOPF);
    I2C1->ICR = I2C_ICR_STOPCF;
    return ok;
}

/* Sensirion CRC-8: polynomial 0x31, init 0xFF. */
static uint8_t sht_crc(const uint8_t *d)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool sht4x_measure(struct sample *out)
{
    const uint8_t cmd = SHT4X_MEASURE_LOW_REP;
    if (!i2c_write(SHT4X_ADDR, &cmd, 1)) {
        g_i2c_errors++;
        return false;
    }

    /* Sleep through the conversion instead of busy-waiting: 33 / 16384 Hz = 2.0 ms. */
    rtc_wakeup_in(33, WUCK_RTC_DIV2);
    enter_stop2_until_rtc();

    uint8_t rx[6];
    if (!i2c_read(SHT4X_ADDR, rx, sizeof rx)) {
        g_i2c_errors++;
        return false;
    }
    if (sht_crc(&rx[0]) != rx[2] || sht_crc(&rx[3]) != rx[5]) {
        g_crc_errors++;
        return false;
    }
    uint32_t t_raw = ((uint32_t)rx[0] << 8) | rx[1];
    uint32_t rh_raw = ((uint32_t)rx[3] << 8) | rx[4];
    /* T = -45 + 175 * raw / 65535, RH = -6 + 125 * raw / 65535 (SHT4x datasheet) */
    int32_t t = -4500 + (int32_t)((17500u * t_raw) / 65535u);
    int32_t rh = -600 + (int32_t)((12500u * rh_raw) / 65535u);
    if (rh < 0) {
        rh = 0;
    } else if (rh > 10000) {
        rh = 10000;
    }
    out->temp_c_x100 = (int16_t)t;
    out->rh_x100 = (uint16_t)rh;
    return true;
}

/* ------------------------------------------------------------------ */

static void process(const struct sample *s)
{
    /* EMA with alpha = 1/4, in integer arithmetic. */
    if (!g_have_filter) {
        g_filtered_x100 = s->temp_c_x100;
        g_have_filter = true;
    } else {
        g_filtered_x100 += (s->temp_c_x100 - g_filtered_x100) / 4;
    }
    g_log.log[g_log.head % LOG_LEN] = *s;
    g_log.head++;
}

int main(void)
{
    board_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\nLab 7: sensor node, period %u s%s\n", PERIOD_S,
           LAB7_KEEP_DEBUG ? " (debug kept alive in Stop)" : "");

#if LAB7_KEEP_DEBUG
    DBGMCU->CR |= DBGMCU_CR_DBG_STOP;
#else
    DBGMCU->CR &= ~(DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY);
    printf("SWD is disabled from here on. To reflash: hold NRST, connect under reset.\n");
#endif

    lp_gpio_init();
    rtc_init();
    i2c1_init();
    /* SRAM2 is not initialised by startup. A Standby-based variant would
     * validate and keep it across wake-ups instead of clearing it. */
    memset(&g_log, 0, sizeof g_log);

    for (;;) {
        struct sample s;
        if (sht4x_measure(&s)) {
            process(&s);
        }
        g_wakes++;

        if (g_wakes % REPORT_EVERY == 0) {
            int32_t f = g_filtered_x100;
            printf("wake %lu: T %ld.%02ld C (filtered), i2c err %lu, crc err %lu\n",
                   (unsigned long)g_wakes, (long)(f / 100), (long)((f < 0 ? -f : f) % 100),
                   (unsigned long)g_i2c_errors, (unsigned long)g_crc_errors);
        }

        rtc_wakeup_in(PERIOD_S, WUCK_CK_SPRE);
        enter_stop2_until_rtc();
    }
}
