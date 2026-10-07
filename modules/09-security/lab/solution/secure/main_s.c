/*
 * Lab 9 reference solution: the Secure world.
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
    sau_region(0, NS_FLASH_START, NS_FLASH_END, false);
    sau_region(1, NSC_START, NSC_END, true);
    sau_region(2, NS_SRAM_START, NS_SRAM_END, false);
    sau_region(3, NS_PERIPH_START, NS_PERIPH_END, false);
    SAU->CTRL = SAU_CTRL_ENABLE_Msk; /* ALLNS = 0: everything not listed is Secure */
    __DSB();
    __ISB();
}

static void gtzc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GTZCEN;
    (void)RCC->AHB1ENR;
    /* After TZEN = 1 every SRAM block is Secure. One VCTR bit = one 256-byte
     * block, so one register = 8 KB. SRAM1 (192 KB) has 24 registers:
     * 0x20020000 is register 16. SRAM2 (64 KB) has 8. */
    for (unsigned i = 16; i < 24; i++) {
        GTZC_MPCBB1->VCTR[i] = 0;
    }
    for (unsigned i = 0; i < 8; i++) {
        GTZC_MPCBB2->VCTR[i] = 0;
    }
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
    /* Both buffers come from the Non-secure world: check that they really
     * are Non-secure memory the caller may access. Without this, NS code
     * could pass a Secure address and make us read or overwrite our own key
     * (the "confused deputy"). */
    if (cmse_check_address_range((void *)msg, len, CMSE_NONSECURE | CMSE_MPU_READ) == NULL ||
        cmse_check_address_range(mac, SECURE_MAC_LEN, CMSE_NONSECURE | CMSE_MPU_READWRITE) == NULL) {
        return -1;
    }
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

    /* Hand over: Non-secure VTOR and MSP from its vector table, then
     * branch to its reset handler with BLXNS. */
    const uint32_t *ns_vectors = (const uint32_t *)NS_FLASH_START;
    SCB_NS->VTOR = NS_FLASH_START;
    __TZ_set_MSP_NS(ns_vectors[0]);
    ns_entry_t ns_reset = (ns_entry_t)cmse_nsfptr_create(ns_vectors[1]);
    ns_reset();

    for (;;) {
    }
}
