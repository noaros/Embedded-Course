/*
 * FreeRTOS configuration for Lab 6b (STM32H723, Cortex-M7 r1p2, GCC ARM_CM4F port).
 * Shared by the starter and the solution: the bugs are in the application.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <stdint.h>
extern uint32_t SystemCoreClock;
void vAssertCalled(const char *file, int line);
void trace_switched_in(void *tcb);
#endif

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1 /* CLZ-based, max 32 priorities */
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128 /* words */
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIME_SLICING                  1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   (48 * 1024)

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2

#define configUSE_TRACE_FACILITY                1
#define configGENERATE_RUN_TIME_STATS           0

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                8
#define configTIMER_TASK_STACK_DEPTH            256

/* Interrupt priorities: 4 implemented bits on STM32. ISRs at priority 0-4
 * are never masked by the kernel and must not call it; ISRs at 5-15 may
 * call ...FromISR() APIs. */
#define configPRIO_BITS                              4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* The application decides what a failed assertion does (vAssertCalled). */
#define configASSERT(x) do { if (!(x)) { vAssertCalled(__FILE__, __LINE__); } } while (0)

/* A tiny built-in tracer (trace.c): records every context switch with a DWT
 * timestamp. pxCurrentTCB is in scope where tasks.c expands this macro. */
#define traceTASK_SWITCHED_IN() trace_switched_in((void *)pxCurrentTCB)

#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_xTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetSchedulerState       1

/* Map the port's handlers onto the CMSIS vector names. */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
