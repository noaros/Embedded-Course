# Precise vs imprecise bus faults

[← Module 8 wiki](README.md) · Lecture notes: [§8.3](../README.md#precise-vs-imprecise-bus-faults) · Lab: [step 4](../lab/README.md#4-make-the-imprecise-fault-precise-30-min)

## Why imprecise faults exist

A store doesn't have to wait for the bus. The core puts the address and data into a **write buffer** and retires the instruction immediately, while the bus interface completes the transfer in the background. This is a large performance win, because most stores never fail.

When a store *does* fail (an unclocked peripheral, an address with nothing behind it, a bus slave returning an error), the error response arrives several cycles later. By then:

```
 cycle:   1        2        3        4        5        6
 core:   STR r1,[r0]  ADDS    LDR      CMP      BNE  ◄── fault taken here
 bus:        └──── write in flight ──────► ERROR
```

- The `STR` has retired. There's nothing to restart and no precise state to report.
- The fault is **imprecise**: `CFSR.IMPRECISERR` is set, `BFARVALID` is **clear**, and BFAR doesn't hold the address.
- The stacked PC points at whatever the core was doing when the error arrived, a few instructions (or a whole function) later.
- An imprecise BusFault is **asynchronous**, so it can also be delayed by priority. If the store happened just before an interrupt, the fault can surface in a different context altogether.

Loads are always precise, because the core has to wait for the data. Fetches are precise too. Only buffered stores produce imprecise faults.

## Finding the store

1. **Look before the PC.** Disassemble a window *before* the stacked PC (`objdump -d` and search back) for stores. A store to an unusual address (an unenabled peripheral, external memory, a computed pointer) is the prime suspect. Often the culprit is in the same function, but not always.
2. **Use the registers.** The base register of the store often still holds the bad address (R0–R3 and R12 are in the frame).
3. Then make the fault precise, using one of the methods below.

## On Cortex-M3/M4: DISDEFWBUF

```c
SCnSCB->ACTLR |= SCnSCB_ACTLR_DISDEFWBUF_Msk;   /* bit 1 */
```

This disables write buffering for accesses to the default memory map. Every store then waits for its bus response, so every bus fault becomes `PRECISERR` with a valid BFAR and an exact PC.

It costs performance on every store, so use it for diagnosis builds, not production. It applies to the *default memory map*, which is what Cortex-M3/M4 code normally runs with.

## On Cortex-M7: bisect with DSB

**The Cortex-M7 has no DISDEFWBUF.** Compare the `ACTLR` definitions in `core_cm7.h` with those in `core_cm4.h`. The M7's store path is more complex: an AXI master with outstanding transactions, a store buffer that merges writes, and the cache. There's no single switch for it.

The workaround is to insert a **data synchronization barrier** after the suspect store:

```c
EXT_REG[0] = value;
__DSB();          /* stall until the store above has completed on the bus */
EXT_REG[1] = other;
__DSB();
```

A `DSB` doesn't complete until every earlier memory access has completed. If the store errors, the core is stalled at the DSB when the error arrives, so the stacked PC lands **right after that DSB**:

- The fault is **still flagged `IMPRECISERR`**, and BFAR is still invalid. The architecture hasn't changed, only the timing.
- The *location* is now pinned to the store before the DSB. Bisect by adding DSBs until one store is isolated.

Costs if you leave them in: each DSB stalls the pipeline until the bus drains, which can take tens to hundreds of cycles for slow peripherals or external memory. Use them for diagnosis, and remove them afterwards, or keep them only where ordering really matters ([Module 5 §5.5](../../05-concurrency-caches/README.md#55-barriers-dmb-dsb-isb)).

## Alternative: map the region as Device and read back

Use the MPU to mark the suspicious region as Device or Strongly-ordered, then **read back** after writing:

```c
p[0] = value;
(void)p[0];       /* a load: precise, and it can't overtake the store to Device memory */
```

The load faults *precisely*, with `PRECISERR`, a valid BFAR and an exact PC. That gives you the address, which a DSB doesn't. The read-back works because loads always wait for their response, and Device-memory ordering keeps the load behind the store.

## Summary

| Technique | Cores | Result | Cost |
| --- | --- | --- | --- |
| Read disassembly before the PC | all | a suspect list | your time |
| `ACTLR.DISDEFWBUF` | M3, M4 | every bus fault precise, with BFAR | every store slower |
| `__DSB()` after suspect stores | all (the only easy option on M7) | PC pinned to the store; still IMPRECISERR, no BFAR | a pipeline stall per DSB |
| MPU Device + read-back | all with an MPU | precise fault on the load, BFAR valid | MPU region setup, an extra load |
