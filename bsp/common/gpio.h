/*
 * Minimal GPIO helpers shared by every board.
 *
 * STM32H7 and STM32L5 use the same GPIO register layout (MODER, OTYPER,
 * OSPEEDR, PUPDR, IDR, ODR, BSRR, AFR[2]), so one set of helpers covers both.
 * Include the device header (via board.h) before this file.
 */
#ifndef COURSE_GPIO_H
#define COURSE_GPIO_H

#include <stdint.h>
#include <stdbool.h>

enum gpio_mode { GPIO_INPUT = 0, GPIO_OUTPUT = 1, GPIO_ALT = 2, GPIO_ANALOG = 3 };
enum gpio_pull { GPIO_NOPULL = 0, GPIO_PULLUP = 1, GPIO_PULLDOWN = 2 };
enum gpio_speed { GPIO_LOW = 0, GPIO_MEDIUM = 1, GPIO_HIGH = 2, GPIO_VERY_HIGH = 3 };

static inline void gpio_set_mode(GPIO_TypeDef *port, unsigned pin, enum gpio_mode mode)
{
    port->MODER = (port->MODER & ~(3u << (2 * pin))) | ((uint32_t)mode << (2 * pin));
}

static inline void gpio_set_pull(GPIO_TypeDef *port, unsigned pin, enum gpio_pull pull)
{
    port->PUPDR = (port->PUPDR & ~(3u << (2 * pin))) | ((uint32_t)pull << (2 * pin));
}

static inline void gpio_set_speed(GPIO_TypeDef *port, unsigned pin, enum gpio_speed speed)
{
    port->OSPEEDR = (port->OSPEEDR & ~(3u << (2 * pin))) | ((uint32_t)speed << (2 * pin));
}

static inline void gpio_set_open_drain(GPIO_TypeDef *port, unsigned pin, bool od)
{
    if (od) {
        port->OTYPER |= 1u << pin;
    } else {
        port->OTYPER &= ~(1u << pin);
    }
}

/* Selects alternate function `af` and switches the pin to ALT mode. */
static inline void gpio_set_af(GPIO_TypeDef *port, unsigned pin, unsigned af)
{
    unsigned shift = 4 * (pin & 7u);
    port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << shift)) | (af << shift);
    gpio_set_mode(port, pin, GPIO_ALT);
}

/* BSRR writes are atomic, so these are safe to call from any context. */
static inline void gpio_high(GPIO_TypeDef *port, unsigned pin) { port->BSRR = 1u << pin; }
static inline void gpio_low(GPIO_TypeDef *port, unsigned pin) { port->BSRR = 1u << (pin + 16); }

/* Not atomic with respect to another context toggling the same pin. */
static inline void gpio_toggle(GPIO_TypeDef *port, unsigned pin)
{
    port->BSRR = (port->ODR & (1u << pin)) ? (1u << (pin + 16)) : (1u << pin);
}

static inline bool gpio_read(const GPIO_TypeDef *port, unsigned pin)
{
    return (port->IDR >> pin) & 1u;
}

#endif /* COURSE_GPIO_H */
