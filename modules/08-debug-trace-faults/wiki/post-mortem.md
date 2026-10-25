# Post-mortem analysis

[← Module 8 wiki](README.md) · Lecture notes: [§8.5](../README.md#85-post-mortem-from-record-to-source-line)

You have a crash report. To turn it into a file, a line and a root cause:

## 1. Classify

Read the handler, CFSR, HFSR and the valid bits first, using [Fault status registers](fault-registers.md). Decide:

- **Precise or not?** If the fault is precise, the PC is the culprit. If it's imprecise, the culprit is *before* the PC ([details](precise-imprecise.md)).
- **Is the PC plausible?** Is it inside `.text` (on the H723, flash at `0x08000000`+, or ITCM)? If not, the crash came from a bad branch or return. Go to [Crash signatures](crash-signatures.md).
- **Is the SP plausible?** If the frame is `0xBADBADBA`, the stack pointer itself was broken.

## 2. Map addresses to source

```sh
$ arm-none-eabi-addr2line -f -C -i -e lab08_case3.elf 0x08000a3e 0x08000b19
write_external_ram
/…/lab/solution/main.c:61
main
/…/lab/solution/main.c:148
```

- `-f` prints function names, `-C` demangles C++ names, and `-i` shows inlined call chains, which matters at `-O2`.
- Pass the PC and the LR together. The LR usually has bit 0 set (the Thumb bit). `addr2line` copes with that, but to be strict, subtract 1 to get an address inside the call instruction.
- `??:0` means the address isn't in this ELF. Either it's the wrong ELF, or the address isn't code.

## 3. Read the instruction

`addr2line` gives you a line, but a line can contain several loads and stores. The disassembly tells you which one:

```sh
$ arm-none-eabi-objdump -d -S --start-address=0x08000a20 --stop-address=0x08000a50 lab08_case3.elf
```

- `-S` interleaves source lines (it needs `-g`).
- Find the instruction at the PC, and identify its base register, for example `str r1, [r3, #4]`. Look up that register's value in the frame (R0–R3, R12). Now you know the address it used.
- For imprecise faults, read **backwards** from the PC to find recent stores.

GDB works without a target too: `arm-none-eabi-gdb lab08.elf`, then `info line *0x08000a3e`, `disassemble /s 0x08000a20,0x08000a50`, `info symbol 0x24001000`.

## 4. Explain the root cause

The faulting line is where the problem *surfaced*, not necessarily where it *started*. Ask:

- Where did the bad value (a null pointer, a garbage address, an odd pointer) come from?
- Was it overwritten? If so, by what? A neighbouring buffer, a stack overflow, a DMA? Use a [watchpoint](coresight.md#watchpoints) or [DebugMonitor](coresight.md#watchpoints-without-a-debugger-debugmonitor) on the location to find out.
- Was the hardware ready? Clock enabled, peripheral out of reset, external memory controller initialised?

## Keep the exact ELF

Decoding needs **the ELF that produced the binary that crashed**. "The same source" isn't enough:

- A different compiler version, flag or include order moves code, and every address changes.
- Timestamps and `__DATE__` change the build output even with identical inputs.
- Hand-built "release" binaries are often not reproducible at all.

Practice:

- **Archive the ELF** (and the `.map` file) of every build that leaves your desk, in CI artefacts or a symbol server, keyed by version.
- **Embed a build ID** in the image: the linker's `--build-id` note, or a hash in a firmware header like Lab 2's stretch goal. Put the build ID in every crash record. The report then names its ELF unambiguously.
- Keep debug info. `-g` doesn't change the code that ends up in flash. It only adds sections to the ELF. You ship the `.bin` or `.hex`, and keep the ELF.

## Backtraces

The frame gives you PC (where) and LR (usually the immediate caller). To go further back, there are three options, from cheapest to most complete:

### Stack-snapshot heuristic

Save 16–64 words of stack above the frame (Lab 8 saves 16). Any word that:

- lies inside `.text` (between `0x08000000` and `__etext` from the map file), and
- has bit 0 set (a Thumb return address)

is a *candidate* return address. Run all of them through `addr2line`.

The result is **probabilistic**. Stale return addresses from earlier calls and function pointers stored in locals also match. Use it to form hypotheses, then check them against the code. A refinement is to check that the instruction just before each candidate is a `BL` or `BLX`. That's Lab 8's second stretch goal.

### Frame pointers / unwind tables

- `-fno-omit-frame-pointer` keeps a chain of frame records (R7 in Thumb code) that a handler can walk. It costs code size and a register.
- `-funwind-tables` emits `.ARM.exidx` tables, which an unwinder can use for exact unwinding without frame pointers. They cost flash, and the unwinder is code that runs in the crash path.

### Core dumps

Save **all** RAM (or the parts that matter) plus the registers. Because RAM is large, this usually goes to flash on the *next* boot, from a RAM region preserved through reset, or from a dedicated dump partition. On the host, load it into GDB with the ELF and you get full backtraces, every variable and every RTOS task. Memfault and Zephyr's coredump subsystem do this, and are worth studying even if you write your own.

| Approach | Target cost | Accuracy | Field use |
| --- | --- | --- | --- |
| PC + LR only | 8 words | 1 level | always |
| Stack snapshot | +16–64 words | heuristic | the default for small devices |
| Frame pointers | code size, 1 register | exact (if all code is built with them) | rare |
| Unwind tables | flash for `.ARM.exidx` + an unwinder | exact | occasional |
| Core dump | flash space for RAM size | everything | best, if you have the flash |

## Worked recipe

```
report:   UsageFault  CFSR 0x00020000 INVSTATE  PC 0x00000000  LR 0x08000a65
step 1:   INVSTATE + PC 0 → a branch to address 0, i.e. a call through a NULL function pointer
step 2:   addr2line LR → the caller, foo.c:212
step 3:   objdump around LR-1: `ldr r3, [r2]; blx r3`, an indirect call through a pointer in RAM
step 4:   that pointer was never assigned; registration happens in an init path that didn't run
fix:      register before enabling the source, or check for NULL and report it
```
