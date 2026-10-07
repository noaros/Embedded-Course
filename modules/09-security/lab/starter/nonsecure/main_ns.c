/*
 * Lab 9: the Non-secure application. Complete TODO 5.
 *
 * Menu (serial, 115200 baud):
 *   s  sign a message through the Secure service and print the MAC
 *   d  "confused deputy": ask the Secure service to write the MAC over the key
 *   a  run task A on its own stack (MPU guard at the bottom): completes
 *   b  run task B on its own stack: recurses into its guard -> MemManage
 *   k  read the key directly from Non-secure code -> SecureFault
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "secure_api.h"

#define TASK_STACK_WORDS 256u

static volatile uint32_t g_ms;
static uint32_t stack_a[TASK_STACK_WORDS] __attribute__((aligned(32)));
static uint32_t stack_b[TASK_STACK_WORDS] __attribute__((aligned(32)));
static const char *volatile g_running_task = "main";

void SysTick_Handler(void)
{
    g_ms++;
}

/* ------------------------------------------------------------------ */
/* MPU stack guards (ARMv8-M, Non-secure MPU)                          */
/* ------------------------------------------------------------------ */

static void mpu_guards_init(void)
{
    /* TODO 5: ARMv8-M MPU stack guards (Module 9 §9.2):
     *   - attribute 0: Normal write-back memory (ARM_MPU_SetMemAttr)
     *   - region 0: the first 32 bytes of stack_a, region 1: of stack_b;
     *     privileged read-only, execute-never (ARM_MPU_RBAR / ARM_MPU_RLAR)
     *   - enable with PRIVDEFENA, and enable the MemManage handler (SHCSR) */
}

/* Runs fn on the process stack `top`, then returns to the main stack. */
__attribute__((naked)) static void run_on_stack(void (*fn)(void), uint32_t *top)
{
    (void)fn;
    (void)top;
    __asm volatile(
        "   push  {r4, lr}          \n"
        "   mrs   r4, control       \n"
        "   msr   psp, r1           \n"
        "   orr   r2, r4, #2        \n" /* SPSEL = 1: Thread mode uses PSP */
        "   msr   control, r2       \n"
        "   isb                     \n"
        "   blx   r0                \n"
        "   msr   control, r4       \n"
        "   isb                     \n"
        "   pop   {r4, pc}          \n");
}

__attribute__((noinline)) static uint32_t recurse(uint32_t depth, uint32_t limit)
{
    volatile uint32_t scratch[16];
    for (unsigned i = 0; i < 16; i++) {
        scratch[i] = depth;
    }
    if (depth >= limit) {
        return scratch[0];
    }
    return recurse(depth + 1, limit) + scratch[depth & 15u];
}

static void task_a(void)
{
    g_running_task = "A";
    (void)recurse(0, 4); /* ~300 bytes of 1 KB: fine */
}

static void task_b(void)
{
    g_running_task = "B";
    (void)recurse(0, UINT32_MAX); /* unbounded: runs into the guard */
}

void MemManage_Handler(void)
{
    uint32_t cfsr = SCB->CFSR;
    printf("\n*** MemManage in task %s: CFSR 0x%08lx", g_running_task, (unsigned long)cfsr);
    if (cfsr & SCB_CFSR_MMARVALID_Msk) {
        uint32_t a = SCB->MMFAR;
        printf(", address 0x%08lx", (unsigned long)a);
        if (a >= (uint32_t)stack_b && a < (uint32_t)stack_b + 32u) {
            printf(" = task B's stack guard");
        } else if (a >= (uint32_t)stack_a && a < (uint32_t)stack_a + 32u) {
            printf(" = task A's stack guard");
        }
    }
    if (cfsr & SCB_CFSR_MSTKERR_Msk) {
        printf(" (MSTKERR: the exception frame itself hit the guard)");
    }
    printf("\nStack overflow contained. Halted.\n");
    for (;;) {
    }
}

/* ------------------------------------------------------------------ */

static void print_hex(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
    printf("\n");
}

static void do_sign(void)
{
    static const char msg[] = "hello from the non-secure world";
    uint8_t mac[SECURE_MAC_LEN];
    int32_t r = secure_sign(msg, sizeof msg - 1u, mac);
    printf("secure_sign(\"%s\") = %ld, MAC ", msg, (long)r);
    print_hex(mac, sizeof mac);
}

static void do_deputy(void)
{
    static const char msg[] = "overwrite";
    uint8_t *key = (uint8_t *)secure_debug_key_address();
    printf("asking the Secure world to write a MAC to %p (its own key)...\n", (void *)key);
    int32_t r = secure_sign(msg, sizeof msg - 1u, key);
    printf("secure_sign returned %ld %s\n", (long)r,
           r < 0 ? "(rejected: pointer check works)" : "(ACCEPTED: the key was overwritten!)");
    do_sign();
}

static void do_read_key(void)
{
    volatile const uint32_t *key = (const uint32_t *)secure_debug_key_address();
    printf("reading Secure address %p from Non-secure code...\n", (const void *)key);
    uint32_t v = *key;
    printf("read 0x%08lx: SAU/GTZC are not protecting the key!\n", (unsigned long)v);
}

int main(void)
{
    /* The Secure world already set the clock. The Non-secure world only
     * enables what it uses. */
    SystemCoreClock = BOARD_SYSCLK_HZ;
    board_uart_init(115200);
    setvbuf(stdout, NULL, _IONBF, 0);
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    gpio_set_mode(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_OUTPUT);
    SysTick_Config(SystemCoreClock / 1000u); /* the Non-secure SysTick (banked) */

    mpu_guards_init();

    printf("\nLab 9: Non-secure application up.\n"
           "  s sign | d confused deputy | a task A | b task B (overflow) | k read key\n");

    uint32_t last = 0;
    for (;;) {
        int c = board_uart_getc();
        switch (c) {
        case 's': do_sign(); break;
        case 'd': do_deputy(); break;
        case 'a':
            run_on_stack(task_a, &stack_a[TASK_STACK_WORDS]);
            g_running_task = "main";
            printf("task A completed\n");
            break;
        case 'b': run_on_stack(task_b, &stack_b[TASK_STACK_WORDS]); break;
        case 'k': do_read_key(); break;
        default: break;
        }
        if (g_ms - last >= 500u) {
            last = g_ms;
            gpio_toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        }
    }
}
