/*
 * Board support for NUCLEO-H723ZG (STM32H723ZG, Cortex-M7 r1p2).
 *
 * Clock tree after board_init():
 *   HSE 8 MHz (bypass, from the ST-LINK MCO)
 *   PLL1: /4 -> 2 MHz, x400 -> 800 MHz VCO, P /2 -> 400 MHz SYSCLK (VOS1)
 *   HCLK (AXI/AHB) 200 MHz, APB1/2/3/4 100 MHz
 *
 * 400 MHz is the VOS1 maximum and needs no option-byte changes. The H723 can
 * run at 520 MHz in VOS0, and at 550 MHz only with the CPUFREQ_BOOST option
 * bit set (RM0468, FLASH_OPTSR2). Module 1's notes walk through that change.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32h723xx.h"
#include "gpio.h"
#include "dwt.h"

#define BOARD_NAME "NUCLEO-H723ZG"

#define BOARD_SYSCLK_HZ 400000000u
#define BOARD_HCLK_HZ   200000000u
#define BOARD_APB1_HZ   100000000u
#define BOARD_APB2_HZ   100000000u
#define BOARD_APB4_HZ   100000000u
/* General-purpose timers on APB1/APB2 run at 2 x PCLK when the APB prescaler is not 1. */
#define BOARD_TIMCLK_HZ 200000000u

/* User LEDs and button. */
#define LED_GREEN_PORT  GPIOB
#define LED_GREEN_PIN   0u
#define LED_YELLOW_PORT GPIOE
#define LED_YELLOW_PIN  1u
#define LED_RED_PORT    GPIOB
#define LED_RED_PIN     14u
#define BUTTON_PORT     GPIOC
#define BUTTON_PIN      13u /* B1, active high */

/* Debug UART: USART3 on PD8 (TX) / PD9 (RX), routed to the ST-LINK virtual COM port. */
#define BOARD_UART USART3

/* Placement attributes for the memory regions the linker script defines. */
#define ITCM_FUNC  __attribute__((section(".itcm_text"), noinline))
#define AXI_BSS    __attribute__((section(".axi_bss")))
#define SRAM1_BSS  __attribute__((section(".sram1_bss")))
#define SRAM2_BSS  __attribute__((section(".sram2_bss")))
#define SRAM4_BSS  __attribute__((section(".sram4_bss")))
#define NOINIT     __attribute__((section(".noinit")))
#define SRAM4_NOINIT __attribute__((section(".sram4_noinit")))

extern uint32_t SystemCoreClock;

/* Clocks to 400 MHz, LEDs, button, UART at 115200 baud, DWT cycle counter, I- and D-cache on. */
void board_init(void);

void board_clock_init(void);
void board_caches(bool icache, bool dcache);
void board_uart_init(uint32_t baud);
void board_uart_putc(char c);
int board_uart_getc(void); /* -1 when no byte is waiting */
void board_delay_ms(uint32_t ms);

#endif /* BOARD_H */
