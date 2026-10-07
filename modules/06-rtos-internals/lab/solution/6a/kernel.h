/*
 * A minimal preemptive kernel for Cortex-M7/M4F/M33 (Lab 6a).
 *
 *   - fixed-priority preemptive scheduling, round-robin time slicing
 *     between tasks of equal priority (0 = idle, KERNEL_MAX_PRIO-1 = highest)
 *   - SVC starts the first task, PendSV switches, SysTick ticks
 *   - task_sleep(), task_yield(), mutexes
 *
 * Kernel-aware interrupts (those that may call kernel_isr_*) must have a
 * priority numerically >= KERNEL_SYSCALL_PRIO.
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdbool.h>
#include <stdint.h>

#define KERNEL_MAX_TASKS 8u
#define KERNEL_MAX_PRIO 8u
#define KERNEL_SYSCALL_PRIO 5u /* BASEPRI level for kernel critical sections */

enum task_state { TASK_READY, TASK_SLEEPING, TASK_BLOCKED, TASK_DEAD };

struct mutex;

struct tcb {
    uint32_t *sp;            /* MUST be first: the PendSV assembly uses offset 0 */
    const char *name;
    uint32_t *stack_base;
    uint32_t stack_words;
    uint8_t prio;            /* current (possibly inherited) priority */
    uint8_t base_prio;       /* priority given at creation */
    uint8_t state;
    uint32_t wake_tick;
    struct mutex *waiting_on;
};

struct mutex {
    struct tcb *owner;
};

typedef void (*task_fn)(void *arg);

struct tcb *task_create(const char *name, task_fn fn, void *arg, uint32_t *stack,
                        uint32_t stack_words, unsigned prio);
void kernel_start(uint32_t tick_hz) __attribute__((noreturn));

void task_sleep(uint32_t ticks);
void task_yield(void);
uint32_t kernel_ticks(void);
struct tcb *task_current(void);

void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

/* Bytes of a task's stack never touched (stack painting). */
uint32_t task_stack_unused(const struct tcb *t);

#endif
