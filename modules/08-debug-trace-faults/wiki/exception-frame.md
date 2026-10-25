# The stacked frame and EXC_RETURN

[← Module 8 wiki](README.md) · Lecture notes: [§8.4](../README.md#84-a-hardfault-handler-that-tells-you-something) · Background: [Module 3 §3.2](../../03-exceptions-nvic/README.md#32-entry-and-return)

## The basic frame

On exception entry the core pushes eight words onto the **current** stack (MSP or PSP), in hardware, before the handler's first instruction:

```
 higher addresses
   ┌────────────┐
   │  (padding) │  ← only if xPSR bit 9 = 1 (see below)
   ├────────────┤
   │   xPSR     │  frame[7]
   │   PC       │  frame[6]   return address = the faulting instruction (precise faults)
   │   LR       │  frame[5]   the faulting function's return address (usually)
   │   R12      │  frame[4]
   │   R3       │  frame[3]
   │   R2       │  frame[2]
   │   R1       │  frame[1]
   │   R0       │  frame[0]   ← SP after entry = `frame`
   └────────────┘
 lower addresses
```

What each word tells you after a fault:

- **PC.** For precise faults and UsageFaults, the faulting instruction. For fetch faults (`IACCVIOL`, `IBUSERR`, `INVSTATE`), the address it tried to execute. For imprecise faults, a [later instruction](precise-imprecise.md).
- **LR.** The value of LR *at the time of the fault*. In a leaf function, or before the prologue has saved it, LR is the return address into the caller, which gives you one level of call stack for free. In a non-leaf function, LR may already have been reused, so treat it as a hint. A value like `0xFFFFFFF9` means the fault happened in a handler, and LR is itself an EXC_RETURN.
- **R0–R3.** Function arguments and scratch values. The base address of a faulting load or store is often one of them.
- **xPSR.** IPSR (bits 8:0) says whether the faulting code was itself in an exception (nonzero) or in Thread mode (0). The T bit (24) should be 1; if it's 0, see `INVSTATE`.

## EXC_RETURN

On entry the core puts a magic value in **LR**. Branching to it (`BX LR` or a POP into PC) tells the core to unstack and return.

| Value (v7-M) | Return to | Stack | Frame |
| --- | --- | --- | --- |
| `0xFFFFFFF1` | Handler mode | MSP | basic (8 words) |
| `0xFFFFFFF9` | Thread mode | MSP | basic |
| `0xFFFFFFFD` | Thread mode | PSP | basic |
| `0xFFFFFFE1` / `E9` / `ED` | the same three | MSP / MSP / PSP | **extended (FP)** |

The meaning of the bits:

| Bit | 1 means | 0 means |
| --- | --- | --- |
| 2 (SPSEL) | frame on **PSP** | frame on **MSP** |
| 3 (Mode) | return to Thread mode | return to Handler mode (nested exception) |
| 4 (FType) | basic 8-word frame | extended 26-word FP frame |

On ARMv8-M (the L552's Cortex-M33) the low byte also encodes the security state and whether extra registers were stacked (for example `0xFFFFFFBC`, `0xFFFFFFFD`). **Test individual bits instead of comparing whole values.**

## Which stack: MSP or PSP?

Handlers always run on MSP, but the faulting code may have been on either stack:

- With no RTOS, everything usually runs on MSP, and bit 2 = 0.
- With an RTOS, tasks run on PSP. A fault in a task has bit 2 = 1, and the frame is on that task's stack.
- A fault inside an ISR (Handler mode) is always on MSP.

That's why the handler entry tests the bit:

```asm
tst   lr, #4        @ EXC_RETURN bit 2
ite   eq
mrseq r0, msp       @ bit 2 = 0: frame is on MSP
mrsne r0, psp       @ bit 2 = 1: frame is on PSP
```

The test must run **before** anything pushes onto MSP. If the handler had a C prologue, MSP would no longer point at the frame. That's why the entry is [naked](crash-handler.md#the-naked-entry).

## The extended (FP) frame

If the interrupted code had used the FPU (`CONTROL.FPCA = 1`), the core reserves an **extended frame**: the basic 8 words, plus S0–S15, FPSCR and one reserved word, which is **26 words** (104 bytes). With lazy stacking (on by default) the space is reserved but the FP registers are only written if the handler touches the FPU.

For a crash handler this matters in one place: **where the caller's stack begins**. That's `frame + 8` words for a basic frame, and `frame + 26` words for an extended one (EXC_RETURN bit 4 = 0). Lab 8's TODO 2 asks exactly this.

A fault during the lazy FP save sets `MLSPERR` or `LSPERR`.

## Alignment padding

The architecture keeps the stack 8-byte aligned on exception entry: on v7-M when `CCR.STKALIGN` is set (the default on M4/M7), and always on v8-M. If SP wasn't 8-byte aligned, the core inserts one padding word **above** the frame and sets **bit 9 of the stacked xPSR**.

So the true pre-exception SP is:

```c
uint32_t words = (exc_return & 0x10u) ? 8u : 26u;
uint32_t pre_sp = (uint32_t)frame + words * 4u + ((frame[7] & (1u << 9)) ? 4u : 0u);
```

For a "words above the frame" snapshot, the only effect of the padding is that the first word may be padding. That's harmless, but useful to know when you read the snapshot.

## Reading a frame in GDB

On a halted target, at a breakpoint or vector catch in HardFault:

```gdb
(gdb) p/x $lr                   # EXC_RETURN
(gdb) x/8xw $msp                # the frame, if bit 2 = 0 (use $psp otherwise)
(gdb) info symbol *(uint32_t*)($msp + 24)   # function containing the stacked PC
```

Some GDB front ends unwind through the exception frame automatically (`bt` shows `<signal handler called>`). If yours doesn't, the commands above do it by hand.
