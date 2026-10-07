# Module 5: Concurrency, memory ordering and caches

**Time:** 6 h (3 h theory, 3 h lab) · **Board:** NUCLEO-H723ZG · **Lab:** [Break it, then fix it](lab/README.md)

This module removes the most common source of "works in debug, fails in release" bugs: wrong assumptions about what the compiler and the memory system are allowed to reorder, merge or cache. Three separate actors can reorder your memory accesses: the compiler, the core's write buffer and load/store pipeline, and the cache. Each needs its own tool.

## Learning objectives

1. State precisely what `volatile` does and doesn't guarantee in C.
2. Use C11 `<stdatomic.h>`, LDREX/STREX and the CMSIS intrinsics to build lock-free data structures.
3. Apply DMB, DSB and ISB at the right points: MPU changes, VTOR writes, peripheral sequences, before WFI.
4. Keep the Cortex-M7 L1 data cache coherent with DMA.

---

## 5.1 Three layers of reordering

```
 your C source
     │  ① compiler: may reorder, merge, hoist or delete accesses to ordinary
     │     variables, as long as a single thread can't tell the difference
     ▼
 instruction stream
     │  ② core: write buffer, out-of-order completion on the bus, speculative
     │     reads of Normal memory (M7)
     ▼
 L1 D-cache (M7)
     │  ③ cache: a DMA master or another core sees RAM, not your cache
     ▼
 RAM / peripherals
```

| Layer | Tool |
| --- | --- |
| ① compiler | `volatile`, C11 atomics, compiler barrier `__asm volatile("" ::: "memory")` |
| ② core | `__DMB()`, `__DSB()`, `__ISB()`, Device memory type, read-back |
| ③ cache | `SCB_CleanDCache_by_Addr`, `SCB_InvalidateDCache_by_Addr`, MPU attributes |

Most bugs come from using the tool for one layer to fix a problem in another: a `volatile` that was supposed to fix a DMA coherency issue, or a `DSB` that was supposed to stop the compiler hoisting a load.

## 5.2 What `volatile` does, and doesn't do

`volatile` tells the compiler that **every access in the source is an observable side effect**. Each read happens, each write happens, the same number of times, and in source order relative to **other volatile accesses**.

It does **not**:

- make a read-modify-write atomic (`counter++` is still load, add, store);
- order volatile accesses relative to **non-volatile** ones;
- emit any barrier instruction, or affect the hardware at all;
- make the cache coherent with DMA.

```c
bool done;                         /* set by an ISR */
void wait(void) { while (!done) { } }
```

At `-O2` GCC loads `done` once and spins forever, because nothing in the loop can change it as far as a single thread can see. `volatile bool done;` fixes **this** loop. But:

```c
uint8_t buf[64];
volatile bool ready;
/* producer */  buf[0] = 42; ready = true;
/* consumer */  if (ready) use(buf[0]);
```

The compiler may sink `buf[0] = 42` below `ready = true`, because `buf` isn't volatile. You need a compiler barrier (or a release store) between them, and on a multi-master system a `DMB` too.

## 5.3 Atomicity on Cortex-M

| Access | Atomic? |
| --- | --- |
| Aligned 8/16/32-bit load or store | yes, single-copy atomic |
| Unaligned 16/32-bit | no (and faults if `UNALIGN_TRP` is set) |
| 64-bit (`LDRD`/`STRD`) | no: two 32-bit accesses, can be interrupted between them (they're restartable, but not atomic with respect to an ISR) |
| Read-modify-write (`x++`, `x |= m`) | no |
| `LDM`/`STM` | no, interruptible |

Three ways to make an RMW atomic:

1. **Critical section** (PRIMASK or BASEPRI, Module 3): simple and always works. It costs latency.
2. **Exclusive access** (ARMv7-M and ARMv8-M, not v6-M):

```c
static inline uint32_t atomic_add_u32(volatile uint32_t *p, uint32_t v)
{
    uint32_t old;
    do {
        old = __LDREXW(p);
    } while (__STREXW(old + v, p) != 0);  /* fails if anything intervened */
    return old + v;
}
```

`STREX` fails if the local exclusive monitor was cleared since the `LDREX`, and **every exception entry or return clears it**. So an ISR that touches anything between the two makes the thread retry. That's the property that makes it correct without masking interrupts.

3. **C11 atomics.** `atomic_fetch_add(&x, 1)` compiles to the same LDREX/STREX loop on v7-M/v8-M Mainline for 8/16/32-bit types. For 64-bit types, and for anything on ARMv6-M, GCC emits calls to `__atomic_*` library functions, which arm-none-eabi does **not** provide. You'll get a link error, or you have to supply them yourself with a critical section. Check with `atomic_is_lock_free()`.

```c
#include <stdatomic.h>
static atomic_uint events;
void ISR(void)  { atomic_fetch_add_explicit(&events, 1, memory_order_relaxed); }
uint32_t take(void) { return atomic_exchange_explicit(&events, 0, memory_order_acquire); }
```

## 5.4 Lock-free queues

The **single-producer, single-consumer** ring from Module 3 needs no atomics at all: each index has one writer, and aligned 32-bit stores are atomic. What it needs is ordering:

- The producer writes the data, **then** publishes the new head (a release).
- The consumer reads the head, **then** reads the data (an acquire).

C11 says exactly that:

```c
atomic_store_explicit(&r->head, h + 1, memory_order_release);
uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
```

On a single-core Cortex-M, these compile to a plain store or load with a `DMB` next to it. The `DMB` is what you'd write by hand.

**Multiple producers** (two ISRs at different priorities pushing into one queue) need the head update to be a real atomic RMW: an LDREX/STREX loop on a reservation index, or a critical section. On ARMv6-M (M0/M0+) only the critical section is available.

## 5.5 Barriers: DMB, DSB, ISB

| Barrier | Guarantees | Typical use |
| --- | --- | --- |
| `DMB` | memory accesses before it are observed before those after it | lock-free data publication, DMA descriptors, shared memory between cores |
| `DSB` | all memory accesses before it **complete** before any instruction after it executes | before `WFI`/`WFE`; after writing a register whose effect must be in place (NVIC disable, SCB, MPU, cache maintenance) |
| `ISB` | flushes the pipeline: later instructions are fetched again under the new context | after changing `CONTROL`, `VTOR`, the MPU, or code in RAM; after enabling the FPU |

The standard recipes, from Arm's *AN321*:

```c
/* MPU reconfiguration */
__DMB();                 /* finish outstanding accesses under the old map */
MPU->CTRL = 0; /* ... program regions ... */ MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
__DSB(); __ISB();        /* new map in force for the next instruction */

/* VTOR relocation */
SCB->VTOR = (uint32_t)ram_vectors;
__DSB(); __ISB();

/* Before sleeping */
__DSB();                 /* all writes (e.g. clearing a flag) done */
__WFI();

/* Self-modifying code / code copied to RAM */
copy_code_to_itcm();
__DSB(); __ISB();        /* plus SCB_InvalidateICache() if the region is I-cached */
```

**Peripheral write ordering.** Peripheral space is Device memory, so writes to it are not reordered with each other and reads aren't speculative. But a write can still sit in the write buffer or a bus bridge after the core has moved on. When the next step depends on the write having **arrived** (enabling an interrupt right after clearing its flag, entering sleep right after disabling a clock), read the same register back. That read can't complete until the write has. On the H7, with three bus bridges between the M7 and an APB peripheral, `DSB` alone isn't enough, because it only drains the core's own buffer.

## 5.6 The Cortex-M7 data cache

The H723's D-cache is 32 KB, 4-way set-associative, with **32-byte lines**. With the default memory map it caches the SRAM region in **write-back, write-allocate** mode:

- A CPU write updates the cache line and marks it **dirty**. RAM gets updated only when the line is evicted or **cleaned**.
- A CPU read of a cached line never goes to RAM. If a DMA changed RAM in the meantime, the CPU sees **stale data** until the line is **invalidated**.
- The core may **speculatively** read Normal memory into the cache at any time, for example a load in a branch that is later not taken.

Cache maintenance operations work on whole lines:

| Operation | Effect | Use for |
| --- | --- | --- |
| **Clean** | write dirty lines back to RAM; keep them valid | before the DMA **reads** a buffer the CPU wrote (TX) |
| **Invalidate** | discard lines, dirty or not | before the CPU **reads** a buffer the DMA wrote (RX) |
| **Clean + invalidate** | write back, then discard | buffers used in both directions |

### The correct RX recipe

```c
/* 1. Before starting the DMA: make sure no dirty line for this buffer can be
 *    evicted on top of the DMA's data later. */
SCB_InvalidateDCache_by_Addr(rx, RX_SIZE);      /* or Clean+Invalidate if the CPU wrote it */
start_rx_dma(rx, RX_SIZE);
wait_for_completion();
/* 2. After completion: discard anything speculatively loaded DURING the transfer. */
SCB_InvalidateDCache_by_Addr(rx, RX_SIZE);
use(rx);
```

Step 2 is the one people leave out, because step 1 "already invalidated". But the M7 can speculatively fill lines of `rx` while the DMA is running, and those lines hold old data. The bug shows up rarely, depending on code layout, and moves when you add a `printf`.

### The partial-line trap

```c
uint8_t rx[100];   /* not a multiple of 32, not 32-aligned */
uint32_t counter;  /* linker puts it in the same line as rx[96..99] */
```

- Invalidating `rx` also invalidates the line holding `counter`. If `counter` was dirty, **its new value is lost**.
- If instead the line holding `counter` is dirty and gets evicted during the DMA, it writes the CPU's **old copy of `rx[96..99]`** over the DMA's data.

`SCB_InvalidateDCache_by_Addr` rounds down to line boundaries. It can't protect the neighbours. The only fix is layout: **align DMA buffers to 32 bytes and pad them to a multiple of 32 bytes.**

```c
#define CACHE_LINE 32u
#define DMA_BUF(type, name, n) \
    type name[((n) * sizeof(type) + CACHE_LINE - 1) / CACHE_LINE * CACHE_LINE / sizeof(type)] \
    __attribute__((aligned(CACHE_LINE)))
```

## 5.7 The MPU alternative: don't cache DMA buffers

Instead of maintaining the cache, mark the DMA region **non-cacheable** with the MPU:

```c
/* 16 KB at 0x30000000 (SRAM1): Normal, non-cacheable, shareable, no execute. */
ARM_MPU_Disable();
ARM_MPU_SetRegion(ARM_MPU_RBAR(0, 0x30000000u),
                  ARM_MPU_RASR(1 /*XN*/, ARM_MPU_AP_FULL, 1 /*TEX*/, 1 /*S*/, 0 /*C*/, 0 /*B*/,
                               0 /*subregions*/, ARM_MPU_REGION_SIZE_16KB));
ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);   /* includes DSB + ISB */
```

TEX = 001, C = 0, B = 0 is **Normal, non-cacheable**. Use it for buffers, not Device memory, so that unaligned accesses and `memcpy` still work.

| | Cache maintenance | Non-cacheable region |
| --- | --- | --- |
| Correctness | easy to get subtly wrong | simple; the hardware guarantees it |
| CPU access speed to the buffer | full cache speed | every access goes to the bus (≈ 5–15× slower on the H7) |
| Maintenance cost | ~1 cycle per line, plus barriers | none |
| Best for | buffers the CPU processes heavily (FIR on ADC samples) | descriptors, small control blocks, buffers the CPU only copies once |

A **write-through** region (TEX = 000, C = 1, B = 0) is the middle ground. Writes go straight to RAM, so TX needs no cleaning, but reads are still cached, so RX still needs invalidation.

ARMv7-M MPU regions must be a power-of-two size, aligned to their size. That's why the linker script puts all DMA buffers together in one output section with matching alignment. Module 9 covers the MPU in depth, including ARMv8-M's base/limit model.

## 5.8 Checklist for any shared data

- [ ] Who writes it, and who reads it: thread, which ISRs, which DMA?
- [ ] Is every read-modify-write protected (atomic, exclusive, or critical section)?
- [ ] Is the data published before the flag or index that announces it (release/acquire, DMB)?
- [ ] Is every polled flag `volatile` or atomic?
- [ ] For DMA: is the buffer 32-byte aligned and padded? Is it cleaned before TX, and invalidated before **and after** RX? Or is it in a non-cacheable region?
- [ ] Is there a read-back or `DSB` where the next step depends on a write having taken effect?

## Knowledge check 5

1. Does `volatile` make `counter++` atomic? Explain.
2. After a DMA peripheral-to-memory transfer completes, should the CPU clean or invalidate the buffer's cache lines?
3. Why must DMA buffers on the M7 be aligned and padded to 32 bytes?
4. Which barrier is required after writing to VTOR or the MPU before the change is guaranteed to take effect for subsequent instructions?

## Further reading

- Arm, *AN321 ARM Cortex-M Programming Guide to Memory Barrier Instructions*.
- ST, *AN4839 Level 1 cache on STM32F7 Series and STM32H7 Series*.
- ST, *AN4838 Managing memory protection unit in STM32 MCUs*.
- Jeff Preshing, "Acquire and Release Semantics" (preshing.com), for the C11 memory model in plain language.
- *ARMv7-M Architecture Reference Manual*, A3.4 "Synchronization and semaphores" and A3.7 "Memory access order".
