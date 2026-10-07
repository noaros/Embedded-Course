/*
 * Reference startup for STM32H723 (the course BSP).
 *
 * Module 2's lab asks you to write your own version from an empty file.
 * Don't open this one until you've finished Lab 2.
 */
#include <stdint.h>

#include "board.h"

/* Symbols defined by bsp/h723/h723.ld */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _siitcm, _sitcm, _eitcm;
extern uint32_t _sbss, _ebss;
extern uint32_t _saxi_bss, _eaxi_bss;
extern uint32_t _ssram1_bss, _esram1_bss;
extern uint32_t _ssram2_bss, _esram2_bss;
extern uint32_t _ssram4_bss, _esram4_bss;

void Reset_Handler(void);
void Default_Handler(void);
void SystemInit(void);
int main(void);
extern void __libc_init_array(void);

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
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
#define VECTOR(name) name,
#define RESERVED() 0,
#include "vectors_h723.h"
#undef VECTOR
#undef RESERVED
};

/*
 * Runs before .data and .bss are initialised, so it must not touch any
 * global variable.
 */
void SystemInit(void)
{
    /* SRAM1/SRAM2 in the D2 domain are clocked only when these bits are set,
     * and Reset_Handler zeroes .sram1_bss/.sram2_bss right after this. */
    RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN;
    (void)RCC->AHB2ENR;

    SCB->VTOR = (uint32_t)g_vectors;
    __DSB();
    __ISB();
}

static void copy_words(uint32_t *dst, const uint32_t *src, const uint32_t *end)
{
    while (dst < end) {
        *dst++ = *src++;
    }
}

static void zero_words(uint32_t *dst, const uint32_t *end)
{
    while (dst < end) {
        *dst++ = 0;
    }
}

__attribute__((noreturn))
void Reset_Handler(void)
{
    /* Enable CP10/CP11 (the FPU) first: with -mfloat-abi=hard the compiler may
     * use FP registers in any function, including the ones below. */
    SCB->CPACR |= (3u << 20) | (3u << 22);
    __DSB();
    __ISB();

    SystemInit();

    copy_words(&_sdata, &_sidata, &_edata);
    copy_words(&_sitcm, &_siitcm, &_eitcm);
    zero_words(&_sbss, &_ebss);
    zero_words(&_saxi_bss, &_eaxi_bss);
    zero_words(&_ssram1_bss, &_esram1_bss);
    zero_words(&_ssram2_bss, &_esram2_bss);
    zero_words(&_ssram4_bss, &_esram4_bss);

    /* Code copied into ITCM must be visible to instruction fetch. */
    __DSB();
    __ISB();

    __libc_init_array();
    main();
    for (;;) {
    }
}

/* Any interrupt without a handler lands here. In the debugger, read IPSR
 * (or `irq` below) to see which exception number fired: subtract 16 for the
 * IRQn value. */
void Default_Handler(void)
{
    volatile uint32_t irq = __get_IPSR();
    (void)irq;
    for (;;) {
    }
}
