# Fault exceptions and escalation

[← Module 8 wiki](README.md) · Lecture notes: [§8.3](../README.md#83-anatomy-of-a-fault) · Background: [Module 3 §3.1](../../03-exceptions-nvic/README.md#31-the-exception-model)

## The fault classes

| Exc # | Handler | Priority | Enabled at reset? | Raised by |
| --- | --- | --- | --- | --- |
| 3 | `HardFault_Handler` | −1 (fixed) | always | escalated faults, vector-table read errors, debug events with no debugger |
| 4 | `MemManage_Handler` | configurable | **no** | MPU violations; instruction fetch from an execute-never region |
| 5 | `BusFault_Handler` | configurable | **no** | bus error responses on fetch, data access, stacking, unstacking or lazy FP save |
| 6 | `UsageFault_Handler` | configurable | **no** | undefined instruction, invalid state, invalid EXC_RETURN, no coprocessor, unaligned, divide by zero, stack limit (v8-M) |
| 7 | `SecureFault_Handler` | configurable | **no** | TrustZone violations (ARMv8-M only, see [Module 9](../../09-security/README.md)) |

NMI (exception 2, priority −2) isn't a fault, but it is the only exception that can preempt HardFault.

The Cortex-M0/M0+ (ARMv6-M) has **only** HardFault, and no CFSR. Diagnosing a crash there relies on the stacked PC and LR alone.

## Escalation

A configurable fault becomes a **HardFault** instead when either of these holds:

1. **Its handler is disabled.** That's the reset state for all three.
2. **It can't preempt.** The fault happened while the core was running at a priority equal to or higher than the fault handler's own priority. Examples: a UsageFault inside a high-priority ISR when UsageFault has a lower priority, or a BusFault inside the BusFault handler.

When this happens, `HFSR.FORCED` is set and the original cause is still recorded in the CFSR. A HardFault with `FORCED` set isn't a diagnosis. It means "look in the CFSR".

Synchronous faults (precise bus errors, MPU violations, UsageFaults) must be handled immediately, so they escalate if they can't preempt. An **imprecise** BusFault is asynchronous: it becomes *pending* and is taken once the priority allows, which can be later still.

## Enable them

```c
SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk     /* bit 16 */
            | SCB_SHCSR_BUSFAULTENA_Msk     /* bit 17 */
            | SCB_SHCSR_USGFAULTENA_Msk;    /* bit 18 */
/* ARMv8-M with TrustZone, from the Secure side: SCB_SHCSR_SECUREFAULTENA_Msk (bit 19) */
```

Do this early in startup, in every product. What you gain:

- **Each class has its own vector.** The handler that ran (IPSR 4, 5 or 6) is itself evidence.
- **Configurable priority.** Set the fault handlers above your application interrupts, with `NVIC_SetPriority(MemoryManagement_IRQn, 0)` and the same for `BusFault_IRQn` and `UsageFault_IRQn`, so a fault in an ISR doesn't escalate.
- **HardFault becomes meaningful.** If HardFault still runs, something serious happened: a fault inside a fault handler, a vector table error, or a debug event.

You can still point all four vectors at one capture routine, as Lab 8 does with `__attribute__((alias))`. IPSR tells the routine which one it is.

Lab 8, step 3 asks you to compare reports with and without these bits. Without them, every report reads `HardFault, HFSR FORCED`. With them, the handler name and a cleared `FORCED` bit carry the information.

## Optional traps in CCR

Two common bugs are silent by default. The CCR can make them fault:

| Bit | Name | Effect when set | Default behaviour |
| --- | --- | --- | --- |
| 3 | `UNALIGN_TRP` | every unaligned LDR/STR/LDRH/STRH raises UsageFault (`UNALIGNED`) | unaligned single loads and stores to Normal memory just work, slower |
| 4 | `DIV_0_TRP` | `SDIV`/`UDIV` by zero raises UsageFault (`DIVBYZERO`) | the result is 0, silently |

Some accesses fault **regardless** of `UNALIGN_TRP`: LDM/STM, LDRD/STRD, and any unaligned access to Device or Strongly-ordered memory. Code that reads a packed `uint64_t` field through a cast pointer can compile to LDRD and fault even though "unaligned access is supported".

Enabling `DIV_0_TRP` turns a silent wrong answer into a crash. Whether that's the right trade depends on the product, but either way, the divide-by-zero path needs handling in the code.

## Lockup

If the core faults when it **can't take a fault**, it enters **lockup**:

- a fault inside the HardFault or NMI handler (execution priority −1 or −2)
- a bus error on the vector fetch for HardFault
- a fault while stacking for a HardFault

In lockup the core stops executing useful code. It repeatedly fetches from a fixed address (`0xEFFFFFFE`), and `DHCSR.S_LOCKUP` is set. Nothing in software runs, including your crash handler. Only a reset gets out:

- a debugger, which can read `DHCSR`, the fault registers and the stacked frame after the fact
- an **independent watchdog**, which is the reason every product needs one
- on some devices, a configurable "reset on lockup" (check the reference manual)

The most common cause of lockup in a crash handler is the handler itself faulting: it read a frame through a bad stack pointer, or it used a stack that had already overflowed. See [Writing a crash handler → robustness](crash-handler.md#robustness-rules).

`CCR.BFHFNMIGN` (bit 8) makes the core ignore data bus faults at priority −1 and −2. It exists for code that deliberately probes for memory from inside HardFault or NMI. Leave it clear otherwise.
