# Fault status registers

[← Module 8 wiki](README.md) · Lecture notes: [§8.3](../README.md#fault-status-registers)

| Register | Address | Contents |
| --- | --- | --- |
| SHCSR | `0xE000ED24` | fault handler enables and active/pending state |
| **CFSR** | `0xE000ED28` | what went wrong (UFSR + BFSR + MMFSR) |
| **HFSR** | `0xE000ED2C` | why it's a HardFault |
| DFSR | `0xE000ED30` | debug event cause (BKPT, DWT match, vector catch, halt) |
| **MMFAR** | `0xE000ED34` | data address of a MemManage fault |
| **BFAR** | `0xE000ED38` | data address of a precise BusFault |
| AFSR | `0xE000ED3C` | implementation-defined auxiliary faults (rarely used) |

## Read order matters

```c
uint32_t cfsr  = SCB->CFSR;    /* 1. status first */
uint32_t hfsr  = SCB->HFSR;
uint32_t mmfar = SCB->MMFAR;   /* 2. then addresses */
uint32_t bfar  = SCB->BFAR;
SCB->CFSR = cfsr;              /* 3. write-1-to-clear what you read */
SCB->HFSR = hfsr;
```

The reasons for this order:

- MMFAR and BFAR mean something **only** when `MMARVALID` or `BFARVALID` is set. If a valid bit is clear, the register holds a stale or meaningless value. Don't print it as if it were the faulting address.
- A later fault **overwrites** the address registers. On some implementations, MMFAR and BFAR are even the same physical register. Read them before you do anything that might fault.
- The CFSR bits are **sticky** until written with 1. If you don't clear them, the next crash report shows a mix of old and new bits.

## CFSR

```
UFSR  [31:16]   bit 25 DIVBYZERO  24 UNALIGNED  20 STKOF  19 NOCP  18 INVPC  17 INVSTATE  16 UNDEFINSTR
BFSR  [15:8]    bit 15 BFARVALID  13 LSPERR  12 STKERR  11 UNSTKERR  10 IMPRECISERR  9 PRECISERR  8 IBUSERR
MMFSR [7:0]     bit  7 MMARVALID   5 MLSPERR  4 MSTKERR  3 MUNSTKERR   1 DACCVIOL     0 IACCVIOL
(all other bits reserved)
```

You can also read the three parts as 8- and 16-bit registers: MMFSR at `0xE000ED28`, BFSR at `0xE000ED29` and UFSR at `0xE000ED2A`.

### MMFSR: MemManage (bits 7:0)

| Bit | Name | Meaning | Stacked PC is |
| --- | --- | --- | --- |
| 0 | IACCVIOL | instruction fetch from an MPU-forbidden or execute-never address | the address it tried to execute |
| 1 | DACCVIOL | data access the MPU forbids | the faulting instruction; MMFAR valid |
| 3 | MUNSTKERR | MPU fault while unstacking on exception return | not meaningful |
| 4 | MSTKERR | MPU fault while stacking on exception entry | not meaningful; the frame may be incomplete |
| 5 | MLSPERR | MPU fault during lazy FP state preservation | the instruction that triggered the FP save |
| 7 | **MMARVALID** | MMFAR holds the faulting address | — |

### BFSR: BusFault (bits 15:8)

| Bit | Name | Meaning | Stacked PC is |
| --- | --- | --- | --- |
| 8 | IBUSERR | bus error on instruction fetch | the address it tried to execute |
| 9 | **PRECISERR** | data bus error, reported synchronously | the faulting instruction; BFAR valid |
| 10 | **IMPRECISERR** | data bus error from a buffered write | **later** than the culprit ([details](precise-imprecise.md)) |
| 11 | UNSTKERR | bus error while unstacking | not meaningful |
| 12 | STKERR | bus error while stacking: SP outside RAM | not meaningful; **don't read the frame** |
| 13 | LSPERR | bus error during lazy FP preservation | — |
| 15 | **BFARVALID** | BFAR holds the faulting address | — |

### UFSR: UsageFault (bits 31:16)

| Bit | Name | Meaning |
| --- | --- | --- |
| 16 | UNDEFINSTR | undefined instruction: corrupt code, or execution wandered into data |
| 17 | **INVSTATE** | tried to execute in ARM state (EPSR.T = 0): branch to an even address, typically a null or corrupt function pointer |
| 18 | INVPC | invalid EXC_RETURN on exception return: the handler's stack is corrupt, or LR was clobbered |
| 19 | NOCP | FPU instruction with the FPU disabled in `CPACR` |
| 20 | STKOF | stack limit (MSPLIM/PSPLIM) exceeded (ARMv8-M only) |
| 24 | UNALIGNED | unaligned access trapped (`CCR.UNALIGN_TRP`, or LDM/STM/LDRD/STRD) |
| 25 | DIVBYZERO | integer divide by zero with `CCR.DIV_0_TRP` set |

For every UsageFault, the stacked PC is the faulting instruction. For `INVSTATE` after a bad branch, it's the branch *target*, so a null function pointer gives PC = 0, and LR points back into the caller.

## HFSR

| Bit | Name | Meaning | Next step |
| --- | --- | --- | --- |
| 1 | VECTTBL | bus error reading the vector table on exception entry | check `VTOR`, the table alignment and the memory it points at (common after a bootloader jump) |
| 30 | **FORCED** | a configurable fault was escalated | read the CFSR; [enable the handlers](fault-exceptions.md#enable-them) |
| 31 | DEBUGEVT | a debug event with halting debug disabled | a leftover `__BKPT()`, a semihosting call, or a DWT/FPB match with no debugger attached |

`FORCED` alone tells you almost nothing. Lab 8 asks what you'd conclude from it alone, and the answer is "a fault happened". Every useful detail is in the CFSR.

## MMFAR and BFAR

- Valid only for **data** accesses: `DACCVIOL` and `PRECISERR`. Instruction-side faults (`IACCVIOL`, `IBUSERR`) never set the valid bits, because the address is the stacked PC.
- The address is the exact byte address of the access. Compare it with the linker map or `nm` output to identify the variable or peripheral.
- On some implementations, MMFAR and BFAR share storage, so a second fault can overwrite the first's address. That's one more reason to read them immediately.

## Decoding by hand

Example: `CFSR = 0x00008200`.

- `0x00008200` = bit 15 (`0x8000`) + bit 9 (`0x0200`)
- → `BFARVALID` + `PRECISERR`: a precise data bus error, and BFAR holds the address.
- → The stacked PC is the faulting instruction. Run `addr2line` on it, and look up BFAR in the memory map.

Example: `CFSR = 0x00020000`.

- bit 17 → `INVSTATE`. Look at the stacked PC (an even address, often 0) and the LR (the caller).

`crash_report.c` in the lab decodes the bit names for you. Practise decoding a few by hand anyway, because in the field you may get a hex dump and nothing else.

GDB can print the registers on a halted target: `x/1xw 0xE000ED28` (CFSR), `x/1xw 0xE000ED2C` (HFSR). Many IDEs have a "Fault Analyzer" view that decodes them.
