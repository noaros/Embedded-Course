/*
 * Lab 6a reference solution: a minimal preemptive kernel.
 */
#include "kernel.h"

#include <string.h>

#include "board.h"

/* BASEPRI value for kernel critical sections: KERNEL_SYSCALL_PRIO in the top
 * 4 bits. Spelled out as a literal because the naked PendSV handler can only
 * use plain assembly. */
#define KERNEL_BASEPRI 0x50
_Static_assert(KERNEL_BASEPRI == (KERNEL_SYSCALL_PRIO << (8 - __NVIC_PRIO_BITS)), "KERNEL_BASEPRI");

#define STACK_PAINT 0xDEADBEEFu
#define IDLE_STACK_WORDS 128u

static struct tcb g_tasks[KERNEL_MAX_TASKS];
static unsigned g_num_tasks;
static uint32_t g_ready_mask;                  /* bit p set <=> some task of priority p is ready */
static uint8_t g_ready_count[KERNEL_MAX_PRIO];
static volatile uint32_t g_ticks;
static uint32_t g_idle_stack[IDLE_STACK_WORDS] __attribute__((aligned(8)));

/* Read by the PendSV and SVC assembly, so it can't be static. */
struct tcb *volatile g_current;

/* ------------------------------------------------------------------ */
/* Critical sections and ready-set bookkeeping                         */
/* ------------------------------------------------------------------ */

static inline uint32_t crit_enter(void)
{
    uint32_t prev = __get_BASEPRI();
    __set_BASEPRI_MAX(KERNEL_BASEPRI);
    __ISB();
    return prev;
}

static inline void crit_exit(uint32_t prev)
{
    __set_BASEPRI(prev);
}

static inline void pend_switch(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
}

static void make_ready(struct tcb *t)
{
    t->state = TASK_READY;
    g_ready_count[t->prio]++;
    g_ready_mask |= 1u << t->prio;
}

static void make_unready(struct tcb *t, enum task_state state)
{
    if (t->state == TASK_READY && --g_ready_count[t->prio] == 0) {
        g_ready_mask &= ~(1u << t->prio);
    }
    t->state = (uint8_t)state;
}

/* Changes a task's effective priority, keeping the ready set consistent. */
static void set_prio(struct tcb *t, uint8_t prio)
{
    if (t->state == TASK_READY) {
        make_unready(t, TASK_READY);
        t->prio = prio;
        make_ready(t);
    } else {
        t->prio = prio;
    }
}

/* ------------------------------------------------------------------ */
/* Scheduler                                                           */
/* ------------------------------------------------------------------ */

/* Called from PendSV with BASEPRI raised. Picks the highest ready priority
 * in O(1) with CLZ, then round-robins among the tasks at that priority,
 * starting after the current one. */
__attribute__((used)) void kernel_select_next(void)
{
    unsigned top = 31u - __CLZ(g_ready_mask); /* idle is always ready: mask != 0 */
    unsigned start = (unsigned)(g_current - g_tasks) + 1u;
    for (unsigned i = 0; i < g_num_tasks; i++) {
        struct tcb *t = &g_tasks[(start + i) % g_num_tasks];
        if (t->state == TASK_READY && t->prio == top) {
            g_current = t;
            return;
        }
    }
}

/*
 * Context switch. Saves R4-R11 (and S16-S31 if the task has an FP frame)
 * plus EXC_RETURN on the outgoing task's PSP, picks the next task, restores
 * the same from its stack. Runs at the lowest priority, so it never
 * interrupts another ISR.
 */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "   mrs     r0, psp                     \n"
        "   isb                                 \n"
        "   movw    r3, #:lower16:g_current     \n"
        "   movt    r3, #:upper16:g_current     \n"
        "   ldr     r2, [r3]                    \n"
        "   tst     lr, #0x10                   \n"
        "   it      eq                          \n"
        "   vstmdbeq r0!, {s16-s31}             \n"
        "   stmdb   r0!, {r4-r11, lr}           \n"
        "   str     r0, [r2]                    \n"
        "   mov     r0, #0x50                   \n" /* KERNEL_BASEPRI */
        "   msr     basepri, r0                 \n"
        "   dsb                                 \n"
        "   isb                                 \n"
        "   bl      kernel_select_next          \n"
        "   mov     r0, #0                      \n"
        "   msr     basepri, r0                 \n"
        "   movw    r3, #:lower16:g_current     \n"
        "   movt    r3, #:upper16:g_current     \n"
        "   ldr     r1, [r3]                    \n"
        "   ldr     r0, [r1]                    \n"
        "   ldmia   r0!, {r4-r11, lr}           \n"
        "   tst     lr, #0x10                   \n"
        "   it      eq                          \n"
        "   vldmiaeq r0!, {s16-s31}             \n"
        "   msr     psp, r0                     \n"
        "   isb                                 \n"
        "   bx      lr                          \n");
}

/* Starts the first task: load its context and "return" to Thread mode on PSP. */
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile(
        "   movw    r3, #:lower16:g_current     \n"
        "   movt    r3, #:upper16:g_current     \n"
        "   ldr     r1, [r3]                    \n"
        "   ldr     r0, [r1]                    \n"
        "   ldmia   r0!, {r4-r11, lr}           \n"
        "   msr     psp, r0                     \n"
        "   isb                                 \n"
        "   mov     r0, #0                      \n"
        "   msr     basepri, r0                 \n"
        "   bx      lr                          \n");
}

void SysTick_Handler(void)
{
    uint32_t key = crit_enter();
    uint32_t now = ++g_ticks;
    bool need_switch = false;

    for (unsigned i = 0; i < g_num_tasks; i++) {
        struct tcb *t = &g_tasks[i];
        if (t->state == TASK_SLEEPING && (int32_t)(now - t->wake_tick) >= 0) {
            make_ready(t);
            if (t->prio > g_current->prio) {
                need_switch = true;
            }
        }
    }
    /* Time slicing: another task shares the running task's priority. */
    if (g_current->state == TASK_READY && g_ready_count[g_current->prio] > 1u) {
        need_switch = true;
    }
    if (need_switch) {
        pend_switch();
    }
    crit_exit(key);
}

/* ------------------------------------------------------------------ */
/* Tasks                                                               */
/* ------------------------------------------------------------------ */

static void task_exit(void)
{
    uint32_t key = crit_enter();
    make_unready(g_current, TASK_DEAD);
    pend_switch();
    crit_exit(key);
    for (;;) {
    }
}

struct tcb *task_create(const char *name, task_fn fn, void *arg, uint32_t *stack,
                        uint32_t stack_words, unsigned prio)
{
    if (g_num_tasks >= KERNEL_MAX_TASKS || prio >= KERNEL_MAX_PRIO || stack_words < 64u) {
        return NULL;
    }
    struct tcb *t = &g_tasks[g_num_tasks];

    for (uint32_t i = 0; i < stack_words; i++) {
        stack[i] = STACK_PAINT;
    }

    /* Top of stack, rounded down to 8 bytes (AAPCS). */
    uint32_t *sp = (uint32_t *)((uintptr_t)(stack + stack_words) & ~(uintptr_t)7u);
    *--sp = 0x01000000u;           /* xPSR: Thumb */
    *--sp = (uint32_t)fn;          /* PC */
    *--sp = (uint32_t)task_exit;   /* LR */
    *--sp = 0;                     /* R12 */
    *--sp = 0;                     /* R3 */
    *--sp = 0;                     /* R2 */
    *--sp = 0;                     /* R1 */
    *--sp = (uint32_t)arg;         /* R0 */
    *--sp = 0xFFFFFFFDu;           /* EXC_RETURN: Thread mode, PSP, basic frame */
    for (int r = 11; r >= 4; r--) {
        *--sp = 0;                 /* R11..R4 */
    }

    t->sp = sp;
    t->name = name;
    t->stack_base = stack;
    t->stack_words = stack_words;
    t->prio = t->base_prio = (uint8_t)prio;
    t->waiting_on = NULL;
    t->state = TASK_DEAD;

    uint32_t key = crit_enter();
    g_num_tasks++;
    make_ready(t);
    crit_exit(key);
    return t;
}

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        __DSB();
        __WFI();
    }
}

void kernel_start(uint32_t tick_hz)
{
    task_create("idle", idle_task, NULL, g_idle_stack, IDLE_STACK_WORDS, 0);

    NVIC_SetPriority(PendSV_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);
    SysTick_Config(SystemCoreClock / tick_hz); /* also sets SysTick to the lowest priority */

    __disable_irq();
    g_current = &g_tasks[g_num_tasks - 1u];
    kernel_select_next();
    __set_BASEPRI(KERNEL_BASEPRI); /* SVC_Handler clears it as the first task starts */
    __enable_irq();

    __asm volatile("svc 0");
    for (;;) {
    }
}

void task_sleep(uint32_t ticks)
{
    uint32_t key = crit_enter();
    g_current->wake_tick = g_ticks + ticks;
    make_unready(g_current, TASK_SLEEPING);
    pend_switch();
    crit_exit(key); /* PendSV runs here */
}

void task_yield(void)
{
    pend_switch();
    __DSB();
    __ISB();
}

uint32_t kernel_ticks(void)
{
    return g_ticks;
}

struct tcb *task_current(void)
{
    return g_current;
}

uint32_t task_stack_unused(const struct tcb *t)
{
    uint32_t n = 0;
    while (n < t->stack_words && t->stack_base[n] == STACK_PAINT) {
        n++;
    }
    return n * 4u;
}

/* ------------------------------------------------------------------ */
/* Mutex with priority inheritance and direct hand-off                 */
/* ------------------------------------------------------------------ */

void mutex_init(struct mutex *m)
{
    m->owner = NULL;
}

void mutex_lock(struct mutex *m)
{
    uint32_t key = crit_enter();
    struct tcb *self = g_current;
    while (m->owner != NULL && m->owner != self) {
        /* Priority inheritance: the owner runs at our priority until it
         * releases the lock, so medium-priority tasks can't keep it off the CPU. */
        if (m->owner->prio < self->prio) {
            set_prio(m->owner, self->prio);
        }
        self->waiting_on = m;
        make_unready(self, TASK_BLOCKED);
        pend_switch();
        crit_exit(key); /* switched out here; resumed once unlock hands us the lock */
        key = crit_enter();
    }
    m->owner = self;
    crit_exit(key);
}

void mutex_unlock(struct mutex *m)
{
    uint32_t key = crit_enter();
    struct tcb *self = g_current;
    if (m->owner != self) {
        crit_exit(key);
        return;
    }
    /* Drop any inherited priority. (Single-lock simplification: a task
     * holding two inherited locks would need the max over both.) */
    if (self->prio != self->base_prio) {
        set_prio(self, self->base_prio);
    }

    struct tcb *next = NULL;
    for (unsigned i = 0; i < g_num_tasks; i++) {
        struct tcb *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->waiting_on == m && (!next || t->prio > next->prio)) {
            next = t;
        }
    }
    /* Hand the lock straight to the highest-priority waiter, so a
     * lower-priority task can't grab it in between. */
    m->owner = next;
    if (next) {
        next->waiting_on = NULL;
        make_ready(next);
    }
    if (next && next->prio > self->prio) {
        pend_switch();
    }
    crit_exit(key);
}
