# Lab 9: Partition and lock

**Board:** NUCLEO-L552ZE-Q · **Time:** 2.5 h · **Builds on:** Module 9, Module 2 (startup, linker scripts)

You split the firmware into a Secure world that owns a signing key and a Non-secure application that can only *use* the key through a narrow API. Then you prove the boundary holds: the Non-secure world faults when it touches the key, can't trick the Secure world into touching it either, and has MPU guards on its task stacks. Finally you lock the debug port with RDP level 1.

> ⚠️ **Read before touching option bytes.** This lab sets `TZEN = 1` and later `RDP = level 1`. Both are reversible on this part, **but each reversal mass-erases the flash**. The instructions below say how. **Never set RDP level 2.** It is permanent and turns the board into a brick for any further lab.

## Memory map

| Range | World | What |
| --- | --- | --- |
| `0x0C000000–0x0C03DFFF` | Secure | Secure image (flash bank 1, Secure watermark) |
| `0x0C03E000–0x0C03FFFF` | Secure, **NSC** | SG veneers only (`.gnu.sgstubs`) |
| `0x08040000–0x0807FFFF` | Non-secure | Non-secure image (flash bank 2) |
| `0x30000000–0x3001FFFF` | Secure | Secure SRAM (the working copy of the key lives here) |
| `0x20020000–0x2003FFFF` | Non-secure | Non-secure SRAM |
| `0x40000000–0x4FFFFFFF` | Non-secure | peripherals via the NS alias |

## Files

```
shared/secure_api.h          the NSC API: secure_sign(), secure_debug_key_address()
starter/secure/              main_s.c (TODO 1-4), startup_s.c, l552_s.ld, sha256.c
starter/nonsecure/           main_ns.c (TODO 5), l552_ns.ld (uses the BSP startup)
```

Building `lab09_starter_ns` also builds `lab09_starter_s` and its import library `lab09_starter_nsclib.o`:

```sh
cmake --build build --target lab09_starter_ns
```

Look at what the import library contains (`arm-none-eabi-nm .../lab09_starter_nsclib.o`): two absolute symbols, the veneer addresses, and nothing else from the Secure image.

## One-time board set-up (STM32CubeProgrammer CLI)

```sh
# 1. Enable TrustZone. (Connect under reset if the board runs firmware that disables SWD.)
STM32_Programmer_CLI -c port=swd mode=UR -ob TZEN=1

# 2. Bank 1 (all 128 pages) Secure, bank 2 Non-secure (start > end = no Secure pages).
STM32_Programmer_CLI -c port=swd mode=UR -ob SECWM1_PSTRT=0x0 SECWM1_PEND=0x7F SECWM2_PSTRT=0x7F SECWM2_PEND=0x0

# 3. Secure boot address = 0x0C000000 (the option byte holds address >> 7).
STM32_Programmer_CLI -c port=swd mode=UR -ob SECBOOTADD0=0x180000

# 4. Check
STM32_Programmer_CLI -c port=swd -ob displ
```

Flash both images. The ELF files carry their own addresses:

```sh
STM32_Programmer_CLI -c port=swd mode=UR -d build/.../secure/lab09_starter_s.elf -d build/.../nonsecure/lab09_starter_ns.elf -rst
```

**Undo it all** (back to `TZEN = 0`, used by the other labs): TrustZone can only be disabled during an RDP regression from level 1 to level 0, which mass-erases the flash:

```sh
STM32_Programmer_CLI -c port=swd mode=UR -ob RDP=0xDC          # level 1 (if not already)
STM32_Programmer_CLI -c port=swd mode=UR -ob RDP=0xAA TZEN=0   # regression: erase + TrustZone off
```

## What you hand in

1. Serial log of: `s` (MAC matches the Python check below), `d` (rejected), `a` (completes), then separate runs of `b` and `k` showing the faults.
2. For `d`: the log **without** your TODO 3 check (key overwritten, MAC changes), and with it.
3. For `k`: the SecureFault report. Which SFSR bit, and what is SFAR?
4. For `b`: the MemManage report. Was it `DACCVIOL` or `MSTKERR`? Explain which one you got and why.
5. RDP level 1 evidence: STM32CubeProgrammer's output when trying to read flash, and what still works.
6. A half-page threat model for this device: which attacks does this lab's partitioning stop, and which does it not?

Check the MAC on the host:

```sh
python3 -c "import hmac,hashlib; print(hmac.new(bytes(range(0x10,0x30)), b'hello from the non-secure world', hashlib.sha256).hexdigest())"
```

## Steps

1. **TODO 1 (SAU) and TODO 4 (jump).** Without the SAU, the Non-secure reset handler's first fetch is from Secure-attributed memory. Predict what happens before you try it. With both done but without TODO 2, the Non-secure startup code immediately accesses its SRAM. Observe what happens, and find the explanation in RM0438's GTZC chapter ("illegal access" behaviour of the MPCBB).
2. **TODO 2 (GTZC MPCBB).** The SAU covers the CPU's view, and the MPCBB the bus's. With both, the menu works.
3. Try `s` and check the MAC with Python. Then try `k`: the SecureFault report should name the key's address.
4. Try `d` **before** doing TODO 3. Then add the `cmse_check_address_range()` checks and try again.
5. **TODO 5 (NS MPU guards).** `a` completes, and `b` stops with a MemManage naming task B's guard.
6. **RDP level 1**: `STM32_Programmer_CLI -c port=swd -ob RDP=0xDC`. Power-cycle, then try to read flash with the programmer and connect a debugger. Then regress with `RDP=0xAA` (and `TZEN=0` if you're finished with TrustZone). Note that this erases everything.

## Stretch goals

- **PSPLIM instead of MPU guards.** Set `PSPLIM` to `stack_b + 32` before running task B. Which fault do you get now (`UFSR.STKOF`)? What does it cost compared with an MPU region?
- **Callback into the Non-secure world.** Add a Secure service that takes a Non-secure function pointer and calls it via `cmse_nonsecure_call`. Show in the disassembly that the compiler clears registers before `BLXNS`.
- **TZIC.** Enable the GTZC illegal-access interrupt for SRAM1 and route it to a Secure handler that logs Non-secure accesses to Secure SRAM blocks.

## Grading

| Criterion | Points |
| --- | --- |
| SAU + GTZC partitioning correct; Non-secure app runs | 4 |
| Secure signing service; MAC verified on the host | 2 |
| Confused-deputy attack demonstrated and fixed | 3 |
| SecureFault on direct key access, explained from SFSR/SFAR | 2 |
| MPU stack guards with a contained overflow | 2 |
| RDP level 1 demonstrated and reversed | 1 |
| Threat model | 2 |
| **Total** | **16** |
