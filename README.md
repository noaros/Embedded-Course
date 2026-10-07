I asked Claude to create an embedded course for me and it came up with a 60 hour course. At first it just provided the course outline, but then I asked it to make the material, which took quite a bit longer! The solutions are in the 'solutions' branch.

Time to order the parts and start working through it!

===============

# Advanced Embedded Software on ARM Cortex-M / STM32

An 11-module, roughly 60-hour course that takes experienced embedded engineers from "it works" to "it's deterministic, debuggable, secure and maintainable" on ARM Cortex-M. Each module has lecture notes, a hands-on lab on real STM32 hardware with starter code that builds out of the box, and a knowledge check.

**Audience:** engineers with 2+ years of embedded C who are comfortable with datasheets, GPIO/UART/SPI/I2C and basic interrupts. No prior RTOS-internals, TrustZone or linker-script experience is assumed.

## Modules

| # | Module | Hours | Board | Lab |
| --- | --- | --- | --- | --- |
| 1 | [Cortex-M architecture](modules/01-cortex-m-architecture/README.md) | 5 | H723 | [Map the machine](modules/01-cortex-m-architecture/lab/README.md): memcpy timing per RAM, DMA into DTCM |
| 2 | [Boot, startup, linker scripts](modules/02-boot-startup-linker/README.md) | 5 | H723 | [Bare-metal from zero](modules/02-boot-startup-linker/lab/README.md): your own startup.c and linker script |
| 3 | [Exceptions and NVIC](modules/03-exceptions-nvic/README.md) | 6 | H723 | [Latency budget](modules/03-exceptions-nvic/lab/README.md): 20 kHz loop, < 200 ns jitter |
| 4 | [DMA and throughput](modules/04-dma-throughput/README.md) | 6 | H723 | [2 MSPS data logger](modules/04-dma-throughput/lab/README.md): ADC → DMA → FIR → 3 Mbaud UART |
| 5 | [Concurrency, memory ordering, caches](modules/05-concurrency-caches/README.md) | 6 | H723 | [Break it, then fix it](modules/05-concurrency-caches/lab/README.md): three bugs in an SPI DMA driver |
| 6 | [RTOS internals](modules/06-rtos-internals/README.md) | 7 | H723 | [300-line kernel + broken FreeRTOS app](modules/06-rtos-internals/lab/README.md) |
| 7 | [Low-power design](modules/07-low-power/README.md) | 4 | L552 | [Coin-cell sensor node](modules/07-low-power/lab/README.md): < 5 µA average |
| 8 | [Debugging, tracing, faults](modules/08-debug-trace-faults/README.md) | 5 | H723 | [Crash detective](modules/08-debug-trace-faults/lab/README.md): five crashes, diagnosed from the dump |
| 9 | [Security: MPU, TrustZone, secure boot](modules/09-security/README.md) | 5 | L552 | [Partition and lock](modules/09-security/lab/README.md): Secure key service, NS stack guards, RDP |
| 10 | [Bootloaders and updates](modules/10-bootloaders-updates/README.md) | 4 | H723 | [Unbrickable update](modules/10-bootloaders-updates/lab/README.md): signed A/B, trial boot, 50 power cuts |
| 11 | [Testing, CI, code quality](modules/11-testing-ci/README.md) | 3 | host | [Pipeline](modules/11-testing-ci/lab/README.md): TDD, sanitizers, coverage, CI, size report |
| — | [Capstone](capstone/README.md) | 4+ | L552/U575 | secure, updatable, low-power data-acquisition node |

**Knowledge checks** are at the end of each module's notes. Attempt them closed-book right after the module. Below 75%, re-run that module's lab before moving on, because later modules build directly on earlier ones.

## Getting started

1. Get the hardware and tools: [docs/setup.md](docs/setup.md).
2. Build everything and flash the blinky for each board:

   ```sh
   cmake -S . -B build -G Ninja
   cmake --build build
   ```

3. Start with [Module 1](modules/01-cortex-m-architecture/README.md).

## Repository layout

```
bsp/                 board support for both boards (startup, linker scripts, clock, UART, GPIO/DWT helpers)
  h723/ l552/        per board; vectors_*.h generated from the CMSIS device headers
  common/            gpio.h, dwt.h, newlib syscalls
cmake/               toolchain file, course_firmware() helper, FreeRTOS/Monocypher fetchers
examples/blinky/     setup check for each board
modules/NN-*/        README.md = lecture notes; lab/README.md = handout; lab/starter/ = code to complete
capstone/            the final project brief and rubric
tools/               host scripts: vector generation, Lab 4 receiver, Lab 10 signing/update/power-cut, size report
docs/                setup guide (and, on the solutions branch, the answer key)
```

## Solutions

Reference solutions for every lab and the knowledge-check answer key live on the **`solutions`** branch, under `modules/NN-*/lab/solution/` and `docs/answer-key.md`. Instructors: hand out `course-material` (or `main`) and keep `solutions` private. With the solutions branch checked out, the same `cmake --build build` builds starters and solutions side by side.

## Status

All firmware compiles warning-free with Arm GNU Toolchain 15.3 (`-Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2`), and the Lab 11 host tests pass under ASan + UBSan. **The labs have not yet been run on hardware.** Register-level details that vary by silicon revision are flagged in the handouts ("verify against the manual"). Please report anything that doesn't behave as described.
