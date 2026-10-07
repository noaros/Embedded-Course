/*
 * Lab 2 reference solution: startup for STM32H723.
 */
#include <stdint.h>

#include "board.h"

int main(void);
extern void __libc_init_array(void);

/* Linker-script symbols: only their addresses mean anything. */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _siitcm, _sitcm, _eitcm;
extern uint32_t _sbss, _ebss;
extern uint32_t _saxi_bss, _eaxi_bss;
extern uint32_t _ssram1_bss, _esram1_bss;
extern uint32_t _ssram2_bss, _esram2_bss;
extern uint32_t _ssram4_bss, _esram4_bss;

void Reset_Handler(void);

/* Records which exception arrived unhandled, for the debugger. */
volatile uint32_t g_unhandled_ipsr;

void Default_Handler(void)
{
    g_unhandled_ipsr = __get_IPSR();
    for (;;) {
    }
}

#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;

#define VECTOR(name) void name(void) WEAK_DEFAULT;
#define RESERVED()
#include "vectors_h723.h"
#undef VECTOR
#undef RESERVED

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)&_estack,  /* 0x00: initial MSP */
    Reset_Handler,       /* 0x04 */
    NMI_Handler,         /* 0x08 */
    HardFault_Handler,   /* 0x0C */
    MemManage_Handler,   /* 0x10 */
    BusFault_Handler,    /* 0x14 */
    UsageFault_Handler,  /* 0x18 */
    0, 0, 0, 0,          /* 0x1C-0x28: reserved */
    SVC_Handler,         /* 0x2C */
    DebugMon_Handler,    /* 0x30 */
    0,                   /* 0x34: reserved */
    PendSV_Handler,      /* 0x38 */
    SysTick_Handler,     /* 0x3C */
#define VECTOR(name) name,
#define RESERVED() 0,
#include "vectors_h723.h"
#undef VECTOR
#undef RESERVED
};

/* 16 system slots + 163 device IRQs. */
_Static_assert(sizeof g_vectors == 179 * 4, "vector table size");

void SystemInit(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN;
    (void)RCC->AHB2ENR; /* read back: the enable takes effect before the first access */
    SCB->VTOR = (uint32_t)g_vectors;
    __DSB();
    __ISB();
}

static void copy(uint32_t *dst, const uint32_t *src, const uint32_t *end)
{
    while (dst < end) {
        *dst++ = *src++;
    }
}

static void zero(uint32_t *dst, const uint32_t *end)
{
    while (dst < end) {
        *dst++ = 0;
    }
}

__attribute__((noreturn))
void Reset_Handler(void)
{
    SCB->CPACR |= (3u << 20) | (3u << 22);
    __DSB();
    __ISB();

    SystemInit();

    copy(&_sdata, &_sidata, &_edata);
    copy(&_sitcm, &_siitcm, &_eitcm);
    zero(&_sbss, &_ebss);
    zero(&_saxi_bss, &_eaxi_bss);
    zero(&_ssram1_bss, &_esram1_bss);
    zero(&_ssram2_bss, &_esram2_bss);
    zero(&_ssram4_bss, &_esram4_bss);
    __DSB();
    __ISB();

    __libc_init_array();
    main();
    for (;;) {
    }
}
