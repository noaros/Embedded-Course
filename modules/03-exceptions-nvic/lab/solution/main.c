/*
 * Lab 3: Latency budget.
 *
 * Two measurements run at the same time, both self-timed with hardware timers
 * so no logic analyser is strictly needed:
 *
 *  Part A, EXTI latency. TIM4 CH1 (PD12) outputs a ~3 kHz pulse; a jumper
 *    feeds it into PA3 (EXTI3). The pulse's rising edge is TIM4's update
 *    event, so TIM4->CNT read as the ISR's first instruction is the latency
 *    from edge to handler, in 5 ns ticks. PE9 is toggled too, for a logic
 *    analyser.
 *
 *  Part B, control-loop jitter. TIM2 fires at 20 kHz. The control ISR reads
 *    TIM2->CNT first: min/max of that value is the entry jitter.
 *
 * Background load you can switch on and off from the serial console:
 *    TIM3 "noise" ISR at ~3 kHz that busy-waits 15 us
 *    USART3 RX interrupt (type anything)
 *    a PRIMASK critical section in the main loop ("legacy driver")
 *    floating-point work in Thread mode (keeps an FP context live)
 *
 * Reference solution. Changes from the starter:
 *   - control loop at priority 0, EXTI at 1, everything else 6 and below
 *   - the legacy critical section masks with BASEPRI (priority >= 2) instead
 *     of PRIMASK, so priorities 0 and 1 are never held off
 *   - TIM2 and EXTI3 handlers run from ITCM through a vector table in DTCM,
 *     so their entry no longer depends on the I-cache or flash wait states
 */
#include <stdio.h>
#include <string.h>

#include "board.h"

#define TICK_NS 5u /* timers run at 200 MHz */
#define CONTROL_HZ 20000u
#define NOISE_HZ 3100u
#define NOISE_BUSY_US 15u
#define LEGACY_CS_US 10u

#define OUT_PORT GPIOE
#define OUT_PIN 9u /* Arduino D6: logic-analyser probe for Part A */

struct stats {
    volatile uint32_t min, max, count;
};

static struct stats g_exti_stats = {UINT32_MAX, 0, 0};
static struct stats g_ctrl_stats = {UINT32_MAX, 0, 0};

/* Runtime switches, toggled from the console. */
static volatile bool g_thread_fp = true;
static volatile bool g_handler_fp = true;
static volatile bool g_noise_on = true;
static volatile bool g_legacy_cs_on = true;
static volatile bool g_ram_vectors = true;

static volatile float g_ctrl_out;

static inline void stats_add(struct stats *s, uint32_t v)
{
    if (v < s->min) {
        s->min = v;
    }
    if (v > s->max) {
        s->max = v;
    }
    s->count++;
}

static void stats_print(const char *name, struct stats *s)
{
    uint32_t mn = s->min, mx = s->max, n = s->count;
    s->min = UINT32_MAX;
    s->max = 0;
    s->count = 0;
    if (n == 0) {
        printf("  %-8s no samples\n", name);
        return;
    }
    printf("  %-8s n=%5lu  min %4lu ns  max %5lu ns  jitter %5lu ns\n", name, (unsigned long)n,
           (unsigned long)(mn * TICK_NS), (unsigned long)(mx * TICK_NS),
           (unsigned long)((mx - mn) * TICK_NS));
}

/* ------------------------------------------------------------------ */
/* Part A: EXTI3 handler, in flash and in ITCM                         */
/* ------------------------------------------------------------------ */

#define EXTI_BODY                                                   \
    uint32_t cnt = TIM4->CNT;                                       \
    gpio_high(OUT_PORT, OUT_PIN);                                   \
    EXTI->PR1 = 1u << 3; /* clear first: see Module 3 §3.6 */       \
    if (g_handler_fp) {                                             \
        g_ctrl_out = g_ctrl_out * 0.5f + 1.0f;                      \
    }                                                               \
    stats_add(&g_exti_stats, cnt);                                  \
    gpio_low(OUT_PORT, OUT_PIN);

void EXTI3_IRQHandler(void) { EXTI_BODY }
ITCM_FUNC void exti3_handler_itcm(void) { EXTI_BODY }

/* ------------------------------------------------------------------ */
/* Part B: 20 kHz control loop                                         */
/* ------------------------------------------------------------------ */

static float g_integral;

#define CONTROL_BODY                                                \
    uint32_t cnt = TIM2->CNT;                                       \
    TIM2->SR = ~TIM_SR_UIF;                                         \
    stats_add(&g_ctrl_stats, cnt);                                  \
    /* A small PI controller standing in for real work. */          \
    float err = 1.0f - g_ctrl_out;                                  \
    g_integral += 0.001f * err;                                     \
    g_ctrl_out = 0.8f * err + g_integral;

void TIM2_IRQHandler(void) { CONTROL_BODY }
ITCM_FUNC void tim2_handler_itcm(void) { CONTROL_BODY }

/* ------------------------------------------------------------------ */
/* Background load                                                     */
/* ------------------------------------------------------------------ */

void TIM3_IRQHandler(void)
{
    TIM3->SR = ~TIM_SR_UIF;
    if (g_noise_on) {
        dwt_delay_cycles(NOISE_BUSY_US * (BOARD_SYSCLK_HZ / 1000000u));
    }
}

#define RX_SIZE 64u
static uint8_t g_rx_buf[RX_SIZE];
static volatile uint32_t g_rx_head, g_rx_tail;

void USART3_IRQHandler(void)
{
    if (USART3->ISR & USART_ISR_ORE) {
        USART3->ICR = USART_ICR_ORECF;
    }
    while (USART3->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)USART3->RDR;
        uint32_t h = g_rx_head;
        if (h - g_rx_tail < RX_SIZE) {
            g_rx_buf[h % RX_SIZE] = c;
            __DMB();
            g_rx_head = h + 1;
        }
    }
}

static int rx_get(void)
{
    uint32_t t = g_rx_tail;
    if (g_rx_head == t) {
        return -1;
    }
    __DMB();
    int c = g_rx_buf[t % RX_SIZE];
    g_rx_tail = t + 1;
    return c;
}

/* Priorities numerically >= this are masked by crit_enter(). 0 and 1 never are. */
#define CRIT_PRIORITY 2u

static inline uint32_t crit_enter(void)
{
    uint32_t prev = __get_BASEPRI();
    __set_BASEPRI_MAX(CRIT_PRIORITY << (8u - __NVIC_PRIO_BITS));
    __ISB();
    return prev;
}

static inline void crit_exit(uint32_t prev)
{
    __set_BASEPRI(prev);
}

/* A driver someone wrote years ago that "needs" interrupts off. It only
 * shares data with priority >= 2 ISRs, so BASEPRI is enough. */
static void legacy_driver_poll(void)
{
    if (!g_legacy_cs_on) {
        return;
    }
    uint32_t key = crit_enter();
    dwt_delay_cycles(LEGACY_CS_US * (BOARD_SYSCLK_HZ / 1000000u));
    crit_exit(key);
}

/* ------------------------------------------------------------------ */
/* Set-up                                                              */
/* ------------------------------------------------------------------ */

static void timers_init(void)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM2EN | RCC_APB1LENR_TIM3EN | RCC_APB1LENR_TIM4EN;
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIODEN | RCC_AHB4ENR_GPIOEEN;
    (void)RCC->AHB4ENR;

    /* TIM2: 20 kHz control-loop tick. */
    TIM2->PSC = 0;
    TIM2->ARR = BOARD_TIMCLK_HZ / CONTROL_HZ - 1u;
    TIM2->DIER = TIM_DIER_UIE;

    /* TIM3: background noise. */
    TIM3->PSC = 0;
    TIM3->ARR = BOARD_TIMCLK_HZ / NOISE_HZ - 1u;
    TIM3->DIER = TIM_DIER_UIE;

    /* TIM4 CH1 on PD12 (AF2): ~3 kHz, 10 us high. PWM mode 1 goes high at
     * the update event (CNT = 0), so CNT measures time since the edge. */
    gpio_set_af(GPIOD, 12, 2);
    gpio_set_speed(GPIOD, 12, GPIO_VERY_HIGH);
    TIM4->PSC = 0;
    TIM4->ARR = 0xFFFFu; /* 16-bit timer: 200 MHz / 65536 = ~3 kHz */
    TIM4->CCR1 = 2000u;
    TIM4->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    TIM4->CCER = TIM_CCER_CC1E;

    gpio_set_mode(OUT_PORT, OUT_PIN, GPIO_OUTPUT);
    gpio_set_speed(OUT_PORT, OUT_PIN, GPIO_VERY_HIGH);

    TIM2->EGR = TIM_EGR_UG;
    TIM3->EGR = TIM_EGR_UG;
    TIM4->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    TIM3->SR = 0;
}

static void exti_init(void)
{
    RCC->APB4ENR |= RCC_APB4ENR_SYSCFGEN;
    (void)RCC->APB4ENR;

    /* EXTICR[0] holds lines 0-3, 4 bits each; port A = 0. */
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI3;

    gpio_set_mode(GPIOA, 3, GPIO_INPUT);
    gpio_set_pull(GPIOA, 3, GPIO_PULLDOWN);

    EXTI->RTSR1 |= 1u << 3;
    EXTI->FTSR1 &= ~(1u << 3);
    EXTI->PR1 = 1u << 3;
    EXTI->IMR1 |= 1u << 3;
}

/*
 * The priority map. Lower number = more urgent. All four at the same level
 * is the "we never thought about it" design.
 */
static void apply_priority_map(void)
{
    NVIC_SetPriorityGrouping(3); /* 4 bits preemption, 0 bits sub-priority */
    NVIC_SetPriority(TIM2_IRQn, 0);   /* control loop: never masked, never blocked */
    NVIC_SetPriority(EXTI3_IRQn, 1);  /* external event: only the control loop beats it */
    NVIC_SetPriority(USART3_IRQn, 6); /* console */
    NVIC_SetPriority(TIM3_IRQn, 8);   /* long-running background work */
}

/*
 * Copy the vector table into DTCM and point the control-loop and EXTI vectors
 * at the ITCM copies of the handlers, or go back to the flash table.
 */
typedef void (*vector_t)(void);
extern const vector_t g_vectors[];

#define NUM_VECTORS (16u + 163u)

/* VTOR needs the table aligned to its size rounded up to a power of two:
 * 179 words = 716 bytes -> 1024. DTCM (.bss) is never cached and has no
 * wait states, so vector fetch is deterministic. */
static vector_t g_ram_vector_table[NUM_VECTORS] __attribute__((aligned(1024)));

static void use_ram_vectors(bool on)
{
    if (on) {
        memcpy(g_ram_vector_table, g_vectors, sizeof g_ram_vector_table);
        g_ram_vector_table[16 + TIM2_IRQn] = tim2_handler_itcm;
        g_ram_vector_table[16 + EXTI3_IRQn] = exti3_handler_itcm;
        __DSB();
        SCB->VTOR = (uint32_t)g_ram_vector_table;
    } else {
        SCB->VTOR = (uint32_t)g_vectors;
    }
    __DSB();
    __ISB();
}

static void print_status(void)
{
    printf("\n[v] ITCM+RAM vectors %-3s  [f] thread FP %-3s  [h] handler FP %-3s\n"
           "[n] noise ISR %-3s         [c] legacy PRIMASK section %-3s\n",
           g_ram_vectors ? "ON" : "off", g_thread_fp ? "ON" : "off", g_handler_fp ? "ON" : "off",
           g_noise_on ? "ON" : "off", g_legacy_cs_on ? "ON" : "off");
}

static void handle_command(int c)
{
    switch (c) {
    case 'v':
        g_ram_vectors = !g_ram_vectors;
        use_ram_vectors(g_ram_vectors);
        break;
    case 'f': g_thread_fp = !g_thread_fp; break;
    case 'h': g_handler_fp = !g_handler_fp; break;
    case 'n': g_noise_on = !g_noise_on; break;
    case 'c': g_legacy_cs_on = !g_legacy_cs_on; break;
    default: break;
    }
    print_status();
}

int main(void)
{
    board_init();
    printf("\nLab 3: latency budget. Jumper PD12 -> PA3.\n");

    timers_init();
    exti_init();
    apply_priority_map();
    use_ram_vectors(g_ram_vectors);

    USART3->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    NVIC_EnableIRQ(USART3_IRQn);
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(EXTI3_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
    TIM3->CR1 = TIM_CR1_CEN;
    TIM4->CR1 = TIM_CR1_CEN;

    print_status();
    uint32_t last = dwt_cycles();
    volatile float acc = 0.0f;

    for (;;) {
        if (g_thread_fp) {
            acc = acc * 0.999f + 1.0f; /* keeps CONTROL.FPCA set in Thread mode */
        }
        legacy_driver_poll();

        int c = rx_get();
        if (c >= 0) {
            handle_command(c);
        }

        if (dwt_cycles() - last >= BOARD_SYSCLK_HZ) {
            last = dwt_cycles();
            printf("--\n");
            stats_print("EXTI", &g_exti_stats);
            stats_print("CONTROL", &g_ctrl_stats);
        }
    }
}
