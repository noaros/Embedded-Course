/*
 * Lab 9: Secure-world startup for STM32L552 (TZEN = 1). The core boots here,
 * in Secure state, from SECBOOTADD0 = 0x0C000000.
 */
#include <stdint.h>

#include "board.h"

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

void Reset_Handler(void);
void Default_Handler(void);
int main(void);
extern void __libc_init_array(void);

#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SecureFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
#define VECTOR(name) void name(void) WEAK_DEFAULT;
#define RESERVED()
#include "vectors_l552.h"
#undef VECTOR
#undef RESERVED

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)&_estack, Reset_Handler, NMI_Handler, HardFault_Handler, MemManage_Handler,
    BusFault_Handler, UsageFault_Handler, SecureFault_Handler, 0, 0, 0, SVC_Handler,
    DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
#define VECTOR(name) name,
#define RESERVED() 0,
#include "vectors_l552.h"
#undef VECTOR
#undef RESERVED
};

__attribute__((noreturn)) void Reset_Handler(void)
{
    SCB->CPACR |= (3u << 20) | (3u << 22);
    __DSB();
    __ISB();
    SCB->VTOR = (uint32_t)g_vectors;

    uint32_t *d = &_sdata;
    const uint32_t *s = &_sidata;
    while (d < &_edata) {
        *d++ = *s++;
    }
    for (d = &_sbss; d < &_ebss; d++) {
        *d = 0;
    }
    __libc_init_array();
    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    for (;;) {
    }
}
