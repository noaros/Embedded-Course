/*
 * Lab 2: your startup file for STM32H723.
 *
 * Rules: do not copy bsp/h723/startup_h723.c. You MAY include the generated
 * vector list "vectors_h723.h" (an X-macro list of every device IRQ in slot
 * order: VECTOR(name) or RESERVED()); read tools/gen_vectors.py to see what
 * it contains.
 *
 * Work through TODO 1..7. Build after each one and check the map file.
 */
#include <stdint.h>

#include "board.h"

int main(void);

/* TODO 1: declare the linker-script symbols you need (_estack, _sidata,
 * _sdata, _edata, _sbss, _ebss, and the ITCM ones you define in TODO B of
 * the linker script). Declare them as `extern uint32_t name;` and use
 * their ADDRESSES (&name), never their values. */
extern uint32_t _estack;

void Reset_Handler(void);

void Default_Handler(void)
{
    for (;;) {
    }
}

/* TODO 2: weak aliases to Default_Handler for the 9 Cortex-M system
 * handlers and every device IRQ handler. */

typedef void (*vector_t)(void);

/* TODO 3: complete the vector table: initial SP, the 15 system exception
 * slots (with zeros where the architecture reserves them), then every
 * device IRQ. Check the size in the map file: it must be 716 bytes. */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)&_estack,
    Reset_Handler,
};

/* TODO 4: SystemInit(): enable the SRAM1/SRAM2 clocks (RCC->AHB2ENR) and
 * point SCB->VTOR at g_vectors. It runs before .data/.bss exist. */

__attribute__((noreturn))
void Reset_Handler(void)
{
    /* TODO 5: enable the FPU (CP10/CP11 in SCB->CPACR) and barrier. */

    /* TODO 6: call SystemInit(), copy .data and .itcm_text from flash,
     * zero .bss, then barrier so code copied to ITCM can be fetched. */

    /* TODO 7: call __libc_init_array() so constructors run. */

    main();
    for (;;) {
    }
}
