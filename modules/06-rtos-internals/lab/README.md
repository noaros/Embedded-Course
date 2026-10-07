# Lab 6: RTOS internals

**Board:** NUCLEO-H723ZG · **Time:** 4 h total · **Builds on:** Module 6, Module 3 (PendSV, BASEPRI)

```
starter/6a/   kernel.c (TODO 1-6), kernel.h, main.c (test application)
starter/6b/   main.c: a FreeRTOS application with three bugs
freertos/     FreeRTOSConfig.h and a small context-switch tracer (trace.c), shared by 6a/6b starters and solutions
```

FreeRTOS V11.2.0 is downloaded automatically at configure time (`cmake/course.cmake`, `course_use_freertos`).

---

## Lab 6a: Write a 300-line kernel

**Time:** 2 h

You implement the core of a preemptive RTOS: task stacks, the first-task start through SVC, the PendSV context switch, an O(1) priority scheduler with round-robin, sleep, and a mutex. The test application then measures your context-switch time and shows whether priority inversion is under control.

### What you hand in

1. Your `kernel.c`.
2. Serial output from at least 30 s of running, showing switch times, the `hi` task's worst lock wait, and per-task unused stack.
3. Context-switch time in cycles and ns. **Target: under 1 µs.** That's under 550 cycles at 550 MHz, and under 400 cycles at the course BSP's 400 MHz. Explain what dominates it.
4. `hi`'s worst lock wait with and without priority inheritance (comment out the `set_prio` call in `mutex_lock`), and why the two numbers differ.

### Steps

Work through the TODOs in order. Each one makes something new observable:

| TODO | What | You'll know it works when |
| --- | --- | --- |
| 1 | `task_create()` initial stack frame | (nothing yet: check it in the debugger's memory view against Module 6 §6.2) |
| 2 | `SVC_Handler`: start the first task | the highest-priority task (`report`) starts and prints in a tight loop: `task_sleep()` can't switch away yet |
| 3 | `kernel_select_next()` | — |
| 4 | `PendSV_Handler` | `ping`/`pong` switch; the LED doesn't blink yet |
| 5 | `SysTick_Handler` | sleeping works: the LED blinks and the report prints each second |
| 6 | `mutex_lock`/`mutex_unlock` | `hi` reports a bounded worst wait |

**Debugging tips:**

- A HardFault right after `svc 0` almost always means a wrong initial frame. Check that the xPSR word has bit 24 (Thumb) set, and that the PC word is the function address (bit 0 set, which the linker provides).
- If the first switch works but the second faults, compare the order of the `stmdb` and `ldmia` register lists. They must mirror each other exactly, including `lr`.
- Use the debugger: halt in `PendSV_Handler` and look at PSP, at `g_current->sp`, and at the memory between them.

### Stretch goals

- Use the FPU in two tasks (`float` accumulators) and check that values survive switches. Then deliberately break the `vstmdbeq` line and watch them corrupt.
- Add `PSPLIM`-style checking in software: on every switch, verify that the outgoing task's SP is above `stack_base` plus 32 bytes, and halt with its name if not.
- Replace the round-robin scan with per-priority linked lists to make selection fully O(1).

---

## Lab 6b: Fix a broken FreeRTOS app

**Time:** 2 h

`starter/6b/main.c` is a small controller application: a 10 ms control loop with a 5 ms deadline, a slow sensor sharing the same bus, a logger fed by a 1 kHz interrupt, and a health report. Field reports say the control loop misses deadlines, the unit "randomly" crashes after a few minutes, and it occasionally locks up.

There are three bugs: a **priority inversion**, a **stack overflow**, and an **ISR calling a non-FromISR API**. The kernel configuration (`freertos/FreeRTOSConfig.h`) is correct. All bugs are in `main.c`.

### What you hand in

For each bug:

1. **Evidence**: trace excerpt, assertion output, or high-water-mark numbers.
2. **Mechanism**, in a few sentences.
3. **Fix**, as a diff.

Plus a final 10-minute run with zero deadline misses, and every task's stack high-water mark showing at least 25% free.

### Tools

- **The built-in tracer** (`freertos/trace.c`) records every context switch, plus TIM6 entry and exit, with DWT timestamps. After the first deadline miss it freezes, and the next report prints the timeline. Read it to see who ran while `control` was waiting.
- **SEGGER SystemView** (with a J-Link) or **Percepio Tracealyzer** (streaming over UART, or snapshot mode) give a graphical view. Use one if you have it; the built-in tracer is enough to solve the lab.
- `uxTaskGetStackHighWaterMark()`: the report prints it for every task.
- `configASSERT` → `vAssertCalled()`: look at what it does in the starter.

### Suggested approach

1. Before chasing symptoms, make failures loud. What do `vAssertCalled()` and `vApplicationStackOverflowHook()` do right now? Fix them so they print and stop.
2. Run again. One bug should now identify itself immediately. Fix it properly, not by removing the assertion.
3. The second bug should report itself shortly after. Size the stack from measurement, not by guessing.
4. The deadline misses remain. Read the trace dump around the first miss: which task holds the bus, and who keeps it from releasing it?

### Stretch goals

- Replace the logger's queue with a **stream buffer** or **direct-to-task notification**, and measure the ISR's duration before and after with DWT.
- Compute the control task's worst-case response time with response-time analysis (Module 6 §6.7), using your measured C values, and compare it with the measured worst.
- Raise TIM6 to priority 3 (above `configMAX_SYSCALL_INTERRUPT_PRIORITY`) while it still calls `xQueueSendFromISR`. What does the port's `vPortValidateInterruptPriority()` check catch?

---

## Grading

| Criterion | Points |
| --- | --- |
| 6a: kernel runs all tasks; sleep, yield, time slicing correct | 5 |
| 6a: context switch measured and explained (< 1 µs) | 2 |
| 6a: mutex works; inheritance effect demonstrated with numbers | 2 |
| 6b: each of the three bugs found with evidence, explained, fixed | 6 (2 each) |
| 6b: 10-minute clean run with stack margins | 1 |
| **Total** | **16** |
