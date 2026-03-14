# CLAUDE.md - Spherical Wavetable Navigator (SWN)

## Project Overview

Firmware for the **Spherical Wavetable Navigator**, a Eurorack-format wavetable synthesis module by 4ms Company. Runs on an **STM32F765ZG** (ARM Cortex-M7 @ 216 MHz) with hardware FPU.

- **Languages:** C (primary), C++ (minimal, one file), ARM assembly (startup/DSP)
- **License:** MIT (software), CC BY-NC-SA 4.0 (hardware)
- **Latest firmware:** v2.2.1

## Build System

Requires the ARM Embedded Toolchain (`arm-none-eabi-gcc`). Tested with versions 8.2.1 and 7.3.1.

| Command | Description |
|---------|-------------|
| `make` | Build firmware (`build/main.elf`, `.hex`, `.bin`) |
| `make DEBUG=1` | Build with `-Og` optimization for debugging |
| `make clean` | Remove build directory |
| `make flash` | Flash via st-flash to `0x08010000` |
| `make wav` | Generate audio bootloader WAV file |
| `make combo` | Merge bootloader + app into single hex |
| `make release` | Build release package (prompts for version) |

VSCode is configured with `make -j4` as the default build task (`.vscode/tasks.json`).

**No CI/CD pipeline exists.** No automated tests. Hardware tests exist in `src/hardware_tests.c` but run on-device.

## Directory Structure

```
src/                  # Application source (.c, .cc)
  drivers/            # Hardware peripheral drivers (ADC, codec, flash, LEDs, etc.)
inc/                  # Header files
  drivers/            # Driver headers
  spheres/            # Built-in wavetable data (large constant arrays)
stm32/
  core/               # ARM CMSIS core (DSP math, FFT)
  device/             # STM32F765 device files, startup assembly, linker script
  periph/             # STM32 HAL peripheral drivers
bootloader/           # Audio bootloader (FSK/QPSK firmware loading)
calc/                 # Offline calculation tools (wavetable generation)
doc/                  # Changelog, hardware test docs
hardware/             # KiCAD schematics and PCB (v1.1)
```

## Key Modules

- **Entry point:** `src/main.c`
- **Audio synthesis:** `src/oscillator.c`, `src/audio_util.c`, `src/fft_filter.c`, `src/resample.c`
- **Wavetable system:** `src/wavetable_editing.c`, `src/wavetable_recording.c`, `src/wavetable_saveload.c`, `src/lfo_wavetable_bank.c`
- **Parameter handling:** `src/params_update.c` (largest file, ~63KB), `src/analog_conditioning.c`, `src/UI_conditioning.c`
- **Preset system:** `src/preset_manager.c`, `src/preset_manager_UI.c`, `src/preset_manager_undo.c`
- **LED control:** `src/led_cont.c`, `src/led_colors.c`
- **Hardware interface:** `inc/hardware_controls.h` (defines all physical controls)
- **Flash storage:** `src/sphere_flash_io.c`, `src/drivers/flash_S25FL127.c`
- **Global definitions:** `inc/globals.h`

## Naming Conventions

Defined in `inc/hardware_controls.h`:

| Style | Usage | Example |
|-------|-------|---------|
| `all_lowercase` | Variables | `valid_fw_version` |
| `ALL_UPPERCASE` | Constants, defines, macros | `HAS_BOOTLOADER` |
| `camelCase` / `Camel` | Typedefs, enums, struct types | `hiresAdcSetup` |
| `o_camelCase` | Object instances (struct variables) | `o_led_cont` |

## Code Style

Configured in `.clang-format`:

- **Base:** LLVM
- **Indentation:** Tabs (width 4)
- **Braces:** Stroustrup (opening brace on same line)
- **Line length:** No limit (`ColumnLimit: 0`)
- **C++ standard:** C++14
- Short `if` statements and blocks allowed on single line
- Format with: `clang-format -i <file>`

## Architecture Notes

- **No RTOS** — cooperative main loop architecture
- **Dual flash layout:** Bootloader at `0x08000000`, application at `0x08010000`
- **Hardware FPU:** Enabled (`-mfpu=fpv5-d16`, `-mfloat-abi=hard`), use hardware float freely
- **Optimization:** Builds with `-O3` by default; use `make DEBUG=1` for `-Og`
- **DMA:** Used extensively for SPI flash and audio codec (SAI)
- **External flash:** S25FL127 (128 Mbit) stores user wavetable spheres
- **Audio codec:** Configured via I2C, audio data via SAI (Serial Audio Interface)

## Common Pitfalls

- Files in `inc/spheres/` contain large constant arrays — avoid unnecessary reads of these
- `params_update.c` is very large (~63KB); changes here affect the entire parameter routing
- The linker script (`stm32/device/STM32F765ZGTx_FLASH.ld`) has critical memory layout constraints — output sections require `ALIGN_WITH_INPUT`
- Bootloader and application share the same MCU but occupy different flash regions; modifying one without awareness of the other can break the combined image
