/*
 * Lab 6a: a minimal preemptive kernel. Complete TODO 1-6.
 *
 * Suggested order: 1 (task stack frame), 2 (SVC: start the first task),
 * 3 (scheduler), 4 (PendSV), 5 (SysTick), 6 (mutex). After TODO 2 the
 * highest-priority task should run; after TODO 4 yields work; after
 * TODO 5 sleeping and time slicing work.
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
__attribute__((unused)) static void set_prio(struct tcb *t, uint8_t prio)
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
    /* TODO 3: set g_current to the next task to run:
     *   - highest ready priority: 31 - __CLZ(g_ready_mask)
     *   - among the READY tasks at that priority, the first one AFTER the
     *     current task in g_tasks[] (wrapping around), so equal priorities
     *     take turns. */
}

/*
 * Context switch. Saves R4-R11 (and S16-S31 if the task has an FP frame)
 * plus EXC_RETURN on the outgoing task's PSP, picks the next task, restores
 * the same from its stack. Runs at the lowest priority, so it never
 * interrupts another ISR.
 */
__attribute__((naked)) void PendSV_Handler(void)
{
    /* TODO 4: the context switch, in assembly (Module 6 §6.2):
     *   save:    r0 = PSP; if EXC_RETURN bit 4 is clear, push s16-s31;
     *            push r4-r11 and lr (EXC_RETURN); g_current->sp = r0
     *   select:  raise BASEPRI to KERNEL_BASEPRI, bl kernel_select_next,
     *            BASEPRI back to 0
     *   restore: r0 = g_current->sp; pop r4-r11 and lr; if bit 4 is clear,
     *            pop s16-s31; PSP = r0; isb; bx lr
     * Load the address of g_current with movw/movt #:lower16: / #:upper16:. */
    __asm volatile("bx lr");
}

/* Starts the first task: load its context and "return" to Thread mode on PSP. */
__attribute__((naked)) void SVC_Handler(void)
{
    /* TODO 2: start the first task: r0 = g_current->sp, pop r4-r11 and lr,
     * PSP = r0, isb, BASEPRI = 0, bx lr (lr now holds 0xFFFFFFFD). */
    __asm volatile("bx lr");
}

void SysTick_Handler(void)
{
    uint32_t key = crit_enter();
    ++g_ticks;
    /* TODO 5: wake every SLEEPING task whose wake_tick has arrived (compare
     * with (int32_t)(now - wake_tick) >= 0 so the wrap is handled), and pend
     * a switch if a woken task outranks g_current, or if another task shares
     * g_current's priority (time slicing). */
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

    /* TODO 1: build the initial stack frame so the task looks as if it had
     * been switched out (Module 6 §6.2): hardware frame (xPSR = 0x01000000,
     * PC = fn, LR = task_exit, R12, R3-R1, R0 = arg), then EXC_RETURN
     * 0xFFFFFFFD, then R11..R4. Start from the top of the stack rounded
     * down to 8 bytes. */
    uint32_t *sp = stack + stack_words;
    (void)fn;
    (void)arg;
    (void)task_exit;

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
    /* TODO 6: while someone else owns m: record waiting_on, block this
     * task (make_unready ... TASK_BLOCKED), pend a switch, and leave the
     * critical section so the switch happens; re-enter it when resumed.
     * Then take ownership. Stretch: priority inheritance via set_prio(). */
    uint32_t key = crit_enter();
    m->owner = g_current;
    crit_exit(key);
}

void mutex_unlock(struct mutex *m)
{
    /* TODO 6: release m. Hand it directly to the highest-priority task
     * blocked on it (make it READY and the new owner) and pend a switch if
     * that task outranks this one. Stretch: drop any inherited priority. */
    uint32_t key = crit_enter();
    m->owner = NULL;
    crit_exit(key);
}
