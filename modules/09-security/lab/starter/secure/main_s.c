/*
 * Lab 9: the Secure world. Complete TODO 1-4.
 *
 * 1. Clocks (the Secure world owns the clock tree).
 * 2. SAU: mark the Non-secure flash, SRAM and peripheral ranges, and the NSC
 *    veneer region.
 * 3. GTZC MPCBB: make the Non-secure part of SRAM1 and all of SRAM2 Non-secure
 *    on the bus side too.
 * 4. Hand the LED, button and LPUART1 pins to the Non-secure world.
 * 5. Let the Non-secure world use the FPU, enable SecureFault.
 * 6. Jump to the Non-secure image at 0x08040000.
 *
 * Secure services (NSC): secure_sign(), secure_debug_key_address().
 */
#include <arm_cmse.h>
#include <string.h>

#include "board.h"
#include "secure_api.h"
#include "sha256.h"

#define NS_FLASH_START 0x08040000u
#define NS_FLASH_END   0x0807FFFFu
#define NSC_START      0x0C03E000u
#define NSC_END        0x0C03FFFFu
#define NS_SRAM_START  0x20020000u
#define NS_SRAM_END    0x2003FFFFu
#define NS_PERIPH_START 0x40000000u
#define NS_PERIPH_END   0x4FFFFFFFu

/* Demo key: in a product it would be provisioned per device and kept in
 * Secure flash behind HDP, never compiled in. The working copy lives in
 * Secure SRAM. */
static uint8_t g_device_key[32] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

/* ------------------------------------------------------------------ */
/* Partitioning                                                        */
/* ------------------------------------------------------------------ */

static void sau_region(uint32_t n, uint32_t start, uint32_t end, bool nsc)
{
    SAU->RNR = n;
    SAU->RBAR = start & SAU_RBAR_BADDR_Msk;
    /* LADDR holds bits [31:5] of the LAST address; the low 5 bits are implied 1s. */
    SAU->RLAR = (end & SAU_RLAR_LADDR_Msk) | (nsc ? SAU_RLAR_NSC_Msk : 0u) | SAU_RLAR_ENABLE_Msk;
}

static void sau_init(void)
{
    /* TODO 1: four SAU regions with sau_region(), then enable the SAU:
     *   0: Non-secure flash   NS_FLASH_START..NS_FLASH_END
     *   1: NSC veneers        NSC_START..NSC_END (NSC = true)
     *   2: Non-secure SRAM    NS_SRAM_START..NS_SRAM_END
     *   3: NS peripherals     NS_PERIPH_START..NS_PERIPH_END
     * Finish with SAU->CTRL = ENABLE, DSB, ISB. */
    (void)sau_region;
}

static void gtzc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GTZCEN;
    (void)RCC->AHB1ENR;
    /* TODO 2: the SAU only sets the CPU's view. On the bus side, every SRAM
     * block is still Secure. Use GTZC_MPCBB1->VCTR[] and GTZC_MPCBB2->VCTR[]
     * to make 0x20020000-0x2002FFFF (SRAM1) and all of SRAM2 Non-secure.
     * One bit = one 256-byte block; one register = 8 KB. */
}

static void release_pins(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN |
                    RCC_AHB2ENR_GPIOGEN;
    (void)RCC->AHB2ENR;
    /* All pins are Secure after TZEN = 1. Release exactly what the
     * Non-secure application needs, and nothing more. */
    GPIOA->SECCFGR &= ~(1u << LED_RED_PIN);
    GPIOB->SECCFGR &= ~(1u << LED_BLUE_PIN);
    GPIOC->SECCFGR &= ~((1u << LED_GREEN_PIN) | (1u << BUTTON_PIN));
    GPIOG->SECCFGR &= ~((1u << 7) | (1u << 8)); /* LPUART1 TX/RX */
}

/* ------------------------------------------------------------------ */
/* Secure services                                                     */
/* ------------------------------------------------------------------ */

SECURE_ENTRY int32_t secure_sign(const void *msg, uint32_t len, uint8_t *mac)
{
    /* TODO 3: msg and mac come from the Non-secure world. Validate both with
     * cmse_check_address_range() before touching them, and return -1 if
     * either is not entirely Non-secure memory the caller may access.
     * Then try the 'd' command in the Non-secure menu, with and without
     * the check. */
    uint8_t out[SECURE_MAC_LEN];
    hmac_sha256(g_device_key, sizeof g_device_key, msg, len, out);
    memcpy(mac, out, sizeof out);
    memset(out, 0, sizeof out);
    return 0;
}

SECURE_ENTRY uint32_t secure_debug_key_address(void)
{
    return (uint32_t)g_device_key;
}

/* ------------------------------------------------------------------ */
/* SecureFault: report through the Non-secure LPUART (if NS set it up)  */
/* ------------------------------------------------------------------ */

static void ns_putc(char c)
{
    if (!(LPUART1_NS->CR1 & USART_CR1_UE)) {
        return;
    }
    while (!(LPUART1_NS->ISR & USART_ISR_TXE_TXFNF)) {
    }
    LPUART1_NS->TDR = (uint8_t)c;
}

static void ns_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            ns_putc('\r');
        }
        ns_putc(*s++);
    }
}

static void ns_puthex(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    ns_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        ns_putc(hex[(v >> i) & 0xFu]);
    }
}

__attribute__((used)) void secure_fault_report(uint32_t exc_return)
{
    uint32_t sfsr = SAU->SFSR;
    uint32_t sfar = SAU->SFAR;
    /* EXC_RETURN bit 6 (S) = 0: the frame is on a Non-secure stack;
     * bit 2 picks PSP or MSP. */
    const uint32_t *frame = NULL;
    if (!(exc_return & (1u << 6))) {
        frame = (const uint32_t *)((exc_return & 4u) ? __TZ_get_PSP_NS() : __TZ_get_MSP_NS());
    }
    ns_puts("\n*** SecureFault: SFSR ");
    ns_puthex(sfsr);
    if (sfsr & SAU_SFSR_SFARVALID_Msk) {
        ns_puts(" SFAR ");
        ns_puthex(sfar);
    }
    if (sfsr & SAU_SFSR_AUVIOL_Msk) {
        ns_puts(" (AUVIOL: Non-secure access to a Secure address)");
    }
    if (sfsr & SAU_SFSR_INVEP_Msk) {
        ns_puts(" (INVEP: Non-secure branch into Secure code outside an SG)");
    }
    if (frame) {
        ns_puts("\n    faulting Non-secure PC ");
        ns_puthex(frame[6]);
    }
    ns_puts("\n    halted.\n");
    for (;;) {
    }
}

__attribute__((naked)) void SecureFault_Handler(void)
{
    __asm volatile(
        "   mov r0, lr                  \n"
        "   b   secure_fault_report     \n");
}

void HardFault_Handler(void) __attribute__((alias("SecureFault_Handler")));

/* ------------------------------------------------------------------ */

typedef void (*ns_entry_t)(void) __attribute__((cmse_nonsecure_call));

int main(void)
{
    board_clock_init(); /* HSI16: the Non-secure world inherits this clock */

    sau_init();
    gtzc_init();
    release_pins();

    /* Non-secure code may use the FPU (CP10/CP11). */
    SCB->NSACR |= SCB_NSACR_CP10_Msk | SCB_NSACR_CP11_Msk;
    SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk;

    /* TODO 4: start the Non-secure world: its vector table is at
     * NS_FLASH_START. Set SCB_NS->VTOR, the Non-secure MSP
     * (__TZ_set_MSP_NS) from word 0, then call word 1 through a
     * cmse_nonsecure_call function pointer (cmse_nsfptr_create). */
    (void)NS_FLASH_START;

    for (;;) {
    }
}
