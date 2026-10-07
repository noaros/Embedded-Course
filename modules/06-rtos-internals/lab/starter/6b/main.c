/*
 * Lab 6b: a FreeRTOS application with three bugs.
 *
 * A small "controller" product:
 *   control  prio 4  every 10 ms: read the sensor bus (1 ms), compute. Deadline: 5 ms after release.
 *   report   prio 3  every 2 s: print health; dumps the trace after the first deadline miss
 *   logger   prio 2  formats log records from the TIM6 ISR; every 50th record it
 *                    "compresses" the log for 30 ms of CPU time
 *   sensor   prio 1  every 20 ms: slow housekeeping read on the same bus (3 ms)
 *   TIM6 ISR prio 6  1 kHz sample tick: queues a record for the logger
 *
 * Symptoms reported from the field: the control loop misses deadlines,
 * "random" crashes after a few minutes, and once in a while the whole unit
 * locks up. Find and fix the three bugs. See lab/README.md.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "board.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "trace.h"

#define CONTROL_PERIOD_MS 10u
#define CONTROL_DEADLINE_MS 5u
#define ISR_ID_TIM6 6u

static SemaphoreHandle_t g_bus_lock;
static QueueHandle_t g_log_queue;

static volatile uint32_t g_control_runs, g_deadline_misses, g_worst_response_us;
static volatile uint32_t g_log_records, g_isr_ticks;
static TaskHandle_t g_tasks[4];

/* ------------------------------------------------------------------ */
/* Failure hooks                                                       */
/* ------------------------------------------------------------------ */

void vAssertCalled(const char *file, int line)
{
    /* Release build: assertions disabled. */
    (void)file;
    (void)line;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/* ------------------------------------------------------------------ */
/* Shared "sensor bus"                                                 */
/* ------------------------------------------------------------------ */

static void bus_transaction(uint32_t us)
{
    xSemaphoreTake(g_bus_lock, portMAX_DELAY);
    dwt_delay_cycles(us * (SystemCoreClock / 1000000u)); /* the bus is busy */
    xSemaphoreGive(g_bus_lock);
}

/* ------------------------------------------------------------------ */
/* Tasks                                                               */
/* ------------------------------------------------------------------ */

static void control_task(void *arg)
{
    (void)arg;
    TickType_t release = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&release, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        uint32_t t0 = DWT->CYCCNT;

        bus_transaction(1000);
        /* ... control law ... */

        uint32_t us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
        if (us > g_worst_response_us) {
            g_worst_response_us = us;
        }
        if (us > CONTROL_DEADLINE_MS * 1000u) {
            g_deadline_misses++;
            trace_freeze(); /* keep the timeline that led up to the first miss */
        }
        g_control_runs++;
    }
}

static void sensor_task(void *arg)
{
    (void)arg;
    for (;;) {
        bus_transaction(3000);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static uint32_t checksum(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h;
}

static void logger_task(void *arg)
{
    (void)arg;
    uint32_t sample;
    uint32_t digest = 0;
    for (;;) {
        if (xQueueReceive(g_log_queue, &sample, portMAX_DELAY) != pdPASS) {
            continue;
        }
        char record[320];
        snprintf(record, sizeof record,
                 "t=%lu sample=%lu runs=%lu misses=%lu worst=%luus digest=%08lx",
                 (unsigned long)xTaskGetTickCount(), (unsigned long)sample,
                 (unsigned long)g_control_runs, (unsigned long)g_deadline_misses,
                 (unsigned long)g_worst_response_us, (unsigned long)digest);
        digest ^= checksum(record);
        g_log_records++;

        if (g_log_records % 50u == 0) {
            dwt_delay_cycles(30u * (SystemCoreClock / 1000u)); /* "compress" the log */
        }
    }
}

static void report_task(void *arg)
{
    (void)arg;
    bool dumped = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        printf("control runs %lu, misses %lu, worst %lu us | log records %lu, isr ticks %lu\n",
               (unsigned long)g_control_runs, (unsigned long)g_deadline_misses,
               (unsigned long)g_worst_response_us, (unsigned long)g_log_records,
               (unsigned long)g_isr_ticks);
        printf("stack free (words): control %lu report %lu logger %lu sensor %lu, heap free %u\n",
               (unsigned long)uxTaskGetStackHighWaterMark(g_tasks[0]),
               (unsigned long)uxTaskGetStackHighWaterMark(g_tasks[1]),
               (unsigned long)uxTaskGetStackHighWaterMark(g_tasks[2]),
               (unsigned long)uxTaskGetStackHighWaterMark(g_tasks[3]),
               (unsigned)xPortGetFreeHeapSize());
        if (g_deadline_misses && !dumped) {
            trace_dump();
            dumped = true;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1 kHz sample tick                                                   */
/* ------------------------------------------------------------------ */

void TIM6_DAC_IRQHandler(void)
{
    trace_isr_enter(ISR_ID_TIM6);
    TIM6->SR = ~TIM_SR_UIF;
    uint32_t tick = ++g_isr_ticks;
    xQueueSend(g_log_queue, &tick, 0);
    trace_isr_exit(ISR_ID_TIM6);
}

static void tim6_init(void)
{
    RCC->APB1LENR |= RCC_APB1LENR_TIM6EN;
    (void)RCC->APB1LENR;
    TIM6->PSC = (BOARD_TIMCLK_HZ / 1000000u) - 1u; /* 1 MHz */
    TIM6->ARR = 1000u - 1u;                         /* 1 kHz */
    TIM6->DIER = TIM_DIER_UIE;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    NVIC_SetPriority(TIM6_DAC_IRQn, 6);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

int main(void)
{
    board_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\nLab 6b: FreeRTOS %s on %s\n", tskKERNEL_VERSION_NUMBER, BOARD_NAME);

    NVIC_SetPriorityGrouping(3); /* all bits preemption, as FreeRTOS requires */

    g_bus_lock = xSemaphoreCreateBinary();
    xSemaphoreGive(g_bus_lock);
    g_log_queue = xQueueCreate(32, sizeof(uint32_t));

    xTaskCreate(control_task, "control", 256, NULL, 4, &g_tasks[0]);
    xTaskCreate(report_task, "report", 512, NULL, 3, &g_tasks[1]);
    xTaskCreate(logger_task, "logger", configMINIMAL_STACK_SIZE, NULL, 2, &g_tasks[2]);
    xTaskCreate(sensor_task, "sensor", 256, NULL, 1, &g_tasks[3]);

    tim6_init();
    vTaskStartScheduler();
    for (;;) {
    }
}
