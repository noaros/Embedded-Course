# Setup

Everything in the course builds with free tools on Linux, macOS or Windows (native or WSL2). Budget an hour for first-time setup.

## Hardware

| Item | Used in | Notes |
| --- | --- | --- |
| **NUCLEO-H723ZG** (or H743ZI) | Modules 1–6, 8, 10, 11 (HIL) | Cortex-M7, ST-LINK V3E on board (SWD + virtual COM port up to several Mbaud) |
| **NUCLEO-L552ZE-Q** (or U575ZI-Q) | Modules 7, 9, capstone | Cortex-M33 with TrustZone; has an IDD jumper for current measurement |
| Logic analyser (Saleae or a cheap FX2 clone with PulseView) | 3, 4, 5 | optional; the labs self-time where they can |
| Power profiler: Nordic PPK2 or ST X-NUCLEO-LPM01A | 7, capstone | needed for the low-power targets |
| SHT40/41/45 breakout (I2C) | 7 | any I2C sensor works with small changes |
| Jumper wires | 3, 5 | |
| USB hub with per-port power switching (`uhubctl` compatible) | 10 | optional; the most realistic power-cut tester |

## Software

| Tool | Version | Install |
| --- | --- | --- |
| **Arm GNU Toolchain** (`arm-none-eabi-gcc`) | 13 or newer | [developer.arm.com → Arm GNU Toolchain downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads); or `apt install gcc-arm-none-eabi libnewlib-arm-none-eabi` (Ubuntu 24.04 ships 13.2), `brew install --cask gcc-arm-embedded`, `pacman -S arm-none-eabi-gcc arm-none-eabi-newlib` |
| **CMake** | 3.25 or newer | package manager or cmake.org |
| Ninja | any | optional but faster: `-G Ninja` |
| Git | any | CMake downloads CMSIS, FreeRTOS, Monocypher and Unity with it |
| Python 3 | 3.9+ | plus `pip install pyserial cryptography` (Labs 4, 10, 11 tools) |
| **STM32CubeProgrammer** | 2.15+ | flashing and option bytes (`STM32_Programmer_CLI`); free from st.com |
| OpenOCD | 0.12+ | alternative for flashing and GDB (targets `stm32h7x.cfg`, `stm32l5x.cfg`) |
| VS Code + **Cortex-Debug** extension | — | debugging; or STM32CubeIDE, or plain `arm-none-eabi-gdb` |
| Host GCC or Clang | — | Lab 11 host tests (ASan/UBSan need a Linux/macOS host or WSL) |
| gcovr, cppcheck | — | Lab 11 coverage and static analysis |

If the toolchain isn't on your `PATH`, point the build at it: `export ARM_GCC_PATH=/opt/arm-gnu-toolchain/bin`.

## Build

From the repository root:

```sh
cmake -S . -B build -G Ninja     # first configure downloads CMSIS (a few MB)
cmake --build build              # every lab's starter (and solution, on the solutions branch)
cmake --build build --target lab03_starter   # just one
```

Outputs land in `build/<path of the lab>/`: `<target>.elf`, `.bin`, `.hex` and `.map`. The build prints per-region memory use and a size line for each image.

**Offline builds.** Point CMake at local checkouts instead of downloading:

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CMSIS_CORE=~/src/CMSIS_6 \
      -DFETCHCONTENT_SOURCE_DIR_CMSIS_DEVICE_H7=~/src/cmsis_device_h7 \
      -DFETCHCONTENT_SOURCE_DIR_CMSIS_DEVICE_L5=~/src/cmsis_device_l5 \
      -DFETCHCONTENT_SOURCE_DIR_FREERTOS_KERNEL=~/src/FreeRTOS-Kernel \
      -DFETCHCONTENT_SOURCE_DIR_MONOCYPHER=~/src/Monocypher
```

Pinned versions: CMSIS_6 v6.2.0, cmsis_device_h7 v1.10.7, cmsis_device_l5 v1.0.7, FreeRTOS-Kernel V11.2.0, Monocypher 4.0.3, Unity v2.6.1.

## Check your setup: blinky

```sh
cmake --build build --target blinky_h723
STM32_Programmer_CLI -c port=swd -d build/examples/blinky_h723.elf -v -rst
```

Open the virtual COM port at **115200 8N1** (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS, `COMx` on Windows):

```sh
python3 -m serial.tools.miniterm /dev/ttyACM0 115200
```

You should see `NUCLEO-H723ZG up, SYSCLK 400000000 Hz` and a tick every 500 ms, with the green LED blinking. Do the same with `blinky_l552` on the L5 board (16 MHz).

## Flashing alternatives

```sh
# OpenOCD
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
        -c "program build/examples/blinky_h723.elf verify reset exit"

# A raw binary at an address (Lab 10 slots)
STM32_Programmer_CLI -c port=swd -d image.signed.bin 0x08020000 -v
```

If firmware has disabled SWD (Lab 7's production build does), connect **under reset**: `STM32_Programmer_CLI -c port=swd mode=UR ...`.

## Debugging with VS Code + Cortex-Debug

`.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "H723 (OpenOCD)",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "openocd",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/examples/blinky_h723.elf",
      "configFiles": ["interface/stlink.cfg", "target/stm32h7x.cfg"],
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/STM32H723.svd",
      "liveWatch": { "enabled": true, "samplesPerSecond": 4 }
    },
    {
      "name": "L552 (ST-LINK GDB server)",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "stlink",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/examples/blinky_l552.elf",
      "runToEntryPoint": "main",
      "svdFile": "${workspaceFolder}/STM32L552.svd"
    }
  ]
}
```

SVD files (peripheral register views) come with STM32CubeIDE, or from ST's website (search for "STM32H7 SVD"). They are not redistributed in this repository.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `arm-none-eabi-gcc: not found` at configure | put the toolchain on `PATH` or set `ARM_GCC_PATH`, then delete `build/` and reconfigure |
| `nano.specs: No such file` | install newlib (`libnewlib-arm-none-eabi` on Debian/Ubuntu) |
| Configure hangs or fails downloading | network/proxy; use the offline variables above |
| No serial output | wrong port or baud; on the L5, PG7/PG8 need `PWR_CR2.IOSV` (the BSP sets it) |
| Board not found by the programmer | update the ST-LINK firmware (STM32CubeProgrammer → Firmware upgrade); try `mode=UR` |
| L552 behaves oddly after Lab 9 | TrustZone is still enabled: see Lab 9 "Undo it all" |
