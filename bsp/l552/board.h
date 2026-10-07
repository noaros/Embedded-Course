/*
 * Board support for NUCLEO-L552ZE-Q (STM32L552ZE, Cortex-M33 with TrustZone-M).
 *
 * This BSP is for TrustZone-disabled operation (TZEN = 0, the factory
 * default). Lab 9 brings its own secure and non-secure startup files.
 *
 * Clock tree after board_init():
 *   HSI16 -> SYSCLK = HCLK = PCLK1 = PCLK2 = 16 MHz, voltage range 2.
 *   Low frequency is deliberate: this board is used for the low-power and
 *   security modules, where active current matters more than speed.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32l552xx.h"
#include "gpio.h"
#include "dwt.h"

#define BOARD_NAME "NUCLEO-L552ZE-Q"

#define BOARD_SYSCLK_HZ 16000000u
#define BOARD_HCLK_HZ   16000000u
#define BOARD_APB1_HZ   16000000u
#define BOARD_APB2_HZ   16000000u

#define LED_GREEN_PORT GPIOC
#define LED_GREEN_PIN  7u
#define LED_BLUE_PORT  GPIOB
#define LED_BLUE_PIN   7u
#define LED_RED_PORT   GPIOA
#define LED_RED_PIN    9u
#define BUTTON_PORT    GPIOC
#define BUTTON_PIN     13u /* B1, active high */

/* Debug UART: LPUART1 on PG7 (TX) / PG8 (RX), routed to the ST-LINK virtual
 * COM port. Port G is powered from VDDIO2, which must be marked valid first. */
#define BOARD_UART LPUART1

#define NOINIT     __attribute__((section(".noinit")))
/* SRAM2 can be retained in Standby (PWR_CR3.RRS), unlike SRAM1. */
#define SRAM2_DATA __attribute__((section(".sram2_noinit")))

extern uint32_t SystemCoreClock;

/* HSI16 clock, LEDs, button, LPUART1 at 115200 baud, DWT cycle counter. */
void board_init(void);

void board_clock_init(void);
void board_uart_init(uint32_t baud);
void board_uart_putc(char c);
int board_uart_getc(void); /* -1 when no byte is waiting */
void board_delay_ms(uint32_t ms);

#endif /* BOARD_H */
