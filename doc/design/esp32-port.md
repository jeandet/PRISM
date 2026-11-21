# The ESP32 port (ESP32-S31-Korvo-1)

**Status:** Proposal — for review before code.

## Why

PRISM runs on one microcontroller board, the STM32H7B3I-DK (prism-stm32). The porting layer
(porting-and-devices.md) claims a port writes only what its target owns; a second board, on
another CPU, another vendor SDK and another build system, is what tests that claim. The user
has an ESP32-S31-Korvo-1 V1.1 on the desk.

First goal (decided): the same two apps as on the STM32 — the widgets demo and the audio
scope — with the scope reading the board's **real microphones** (the STM32 board has none; it
reads an ADC pin). The MP3 player (SD card, decoder, codec, PRISM UI) comes after, on this board.

## The board

| | |
|---|---|
| Chip | ESP32-S31 (WROOM-3 module): 2 × RISC-V at 320 MHz, FPU, 512 KB SRAM |
| Memory | 16 MB PSRAM, 16 MB flash |
| Screen | 4.3" 800×480, parallel RGB, RGB565 on a 16-bit bus (subboard SUB3) |
| Touch | GT1151, I2C |
| Audio | ES8389 codec: 2 microphones, 2 × 3 W speaker amplifiers |
| Graphics | PPA (fill, blend, scale/rotate/format conversion), 2D-DMA, JPEG codec |
| Links | USB-C to a USB-UART bridge (flash, console), USB 2.0 HS host |

Software: ESP-IDF supports the chip on its master branch only (heading for v6.1); its
toolchain is GCC 15. Espressif's board-support component `espressif/esp32_s31_korvo_1` sets up
the panel (returns an `esp_lcd` panel handle), the touch panel and the codec, and builds without
LVGL (`BSP_CONFIG_NO_GRAPHIC_LIB=1`). No emulator models this chip: tests run on the board.

## Goals

- `prism-esp32`, a board repository shaped like prism-stm32: board code only.
- Both apps run from the **same sources** as on the STM32.
- PRISM builds unchanged for the ESP32 except for what a second port shows is missing.
- The profiler works there: the CPU cycle counter as its clock, the dump on the USB-UART.

## Non-goals

- Wi-Fi, Bluetooth, camera, USB host, the speakers (the MP3 player brings the speakers).
- Using LVGL, or Espressif's `esp_lvgl_port`.
- A port of PRISM's POSIX layer (ports/freertos): ESP-IDF has POSIX threads of its own.
- Frame-rate work beyond measuring (see Risks).

## Anti-goals

- Board code in PRISM, or ESP-IDF headers reaching PRISM's headers.
- A second, hand-kept list of PRISM's source files (it would drift from PRISM's meson build).

## Approaches considered (the build)

ESP-IDF builds with CMake and owns the link (its linker scripts, its generated `sdkconfig`);
PRISM builds with Meson.

- **A. An ESP-IDF project; PRISM built by Meson inside it (chosen).** prism-esp32 is a plain
  ESP-IDF project. A `prism` component runs Meson on the PRISM subproject (an
  `ExternalProject`), with a cross file written at configure time from ESP-IDF's own compiler
  and flags, and imports the static libraries. PRISM's build stays the only description of
  PRISM. Cost: the glue; flags must match ESP-IDF's (ABI, `-march`, C library).
- **B. List PRISM's sources in the component's CMake.** Quick to start; drifts from PRISM's
  build at the first new file, and loses its options (`profiling`, `sdl=disabled`, …).
- **C. Meson on top, ESP-IDF's libraries linked in.** Fights ESP-IDF, which generates its link
  step; not maintainable.

## Design

### Repository

```
prism-esp32/                 ESP-IDF project
  CMakeLists.txt             project(prism-esp32)
  main/                      app_main(): picks the app, starts frame_app
  components/prism/          CMakeLists.txt: Meson via ExternalProject, imported libs
  boards/korvo1/             display, touch, microphone source, UART stream, profile clock
  subprojects/prism          git submodule (as in prism-stm32)
  sdkconfig.defaults         PSRAM on, C++ exceptions on, CPU 320 MHz, FreeRTOS tick 1000 Hz
```

### Shared apps move into PRISM

`app/widgets_app.hpp`, the audio scope (`audio_scope.*`, `spectrum.*`, `sample_source.hpp`,
`synthetic_source.*`) move from prism-stm32 to PRISM (`examples/embedded/`), as
porting-and-devices.md already planned. The scope's sample source stays a parameter: the STM32
passes its ADC, the ESP32 its microphones.

- *Our call:* PRISM builds them as a small static library on every target (not only when SDL
  is on, as `examples/` is today), exported as a dependency both boards link; neither board
  lists their files.
- *Our call:* the scope's FFT is CMSIS-DSP (`arm_rfft_fast_f32`). It moves from prism-stm32's
  `third_party/` into PRISM with the scope, used by this library only. Its portable C path builds
  on RISC-V and on the desktop (prism-stm32's host build already does); on the M7 it keeps its
  DSP instructions.

### Display: ARGB8888 rendered, RGB565 shown

PRISM's rasterizer draws ARGB8888 only (`FrameSink::back()` is `uint32_t` pixels); the panel
takes RGB565. *Our call:* render into one ARGB8888 frame in PSRAM (1.5 MB), and on `present()`
convert it into the panel's RGB565 frame buffer (two, 750 KB each, owned by the `esp_lcd` RGB
driver) with the **PPA** (its scale-rotate-mirror engine converts formats), waiting for it, then
show that buffer. The CPU conversion is the fallback if the PPA misbehaves on this young chip.

The upgrade, later and in PRISM: a rasterizer compiled for RGB565 (porting-and-devices.md,
"pixel formats as a type"), which halves memory traffic and removes the conversion.

### Touch, microphones, button, UART

- Touch: the BSP's GT1151 handle, read in `TouchSource::read()`.
- Microphones: the BSP's codec device (`esp_codec_dev`), 16 kHz (the BSP's recommendation),
  read in blocks into the scope's sample source on its own thread, as the STM32's ADC is.
- Profiler: the dump goes out through `UartStream` over UART0 (the USB-UART bridge) at
  921600; one of the four ADC buttons (PLAY) triggers it.

### PRISM's build for this target

A cross file for `riscv32-esp-elf` (generated, see A), `-Dsdl=disabled -Dio=disabled
-Dtests=disabled`, `profiling` passed through.

The target is bare metal to Meson (`host_machine.system() == 'none'`), and today that also
builds PRISM's own FreeRTOS kernel and POSIX layer (`ports/`), which would clash with
ESP-IDF's. *Our call:* a new option, `freertos_port` (feature, auto = on for `none`), turns
`ports/` off; the ESP32 build disables it, the STM32 and QEMU builds are unchanged. On `none`,
PRISM already leaves the profiler's clock to the port, which is what this target wants. ESP-IDF turns C++ exceptions off by default;
*our call:* turn them on in `sdkconfig.defaults` (as the STM32 build has them), so PRISM is
compiled as everywhere else. Whether PRISM can run without exceptions is a separate question.

The profiler's clock (`prism::platform::profile_ticks()`) for this target: the RISC-V cycle
counter through ESP-IDF's `esp_cpu_get_cycle_count()`, extended to 64 bits as on the M7, in
prism-esp32 (the board owns it: ports/freertos's M7 clock does not apply).

## Using it (decided: as friendly as it can be)

The person holding the board should go from a fresh clone to a running demo without installing
ESP-IDF or knowing it exists.

- **Flash without building.** CI builds both firmwares and publishes them (an artifact on every
  run, a GitHub release when tagged), each as one merged image written at address 0. Flash it
  from the browser (Espressif's web flasher, nothing to install, works on macOS, Linux and
  Windows) or with `esptool.py write_flash 0x0 <file>` (`pip install esptool`).
- **Build with one command.** `scripts/build.sh` runs ESP-IDF from the repository's container
  image (the pinned commit is baked in, as prism-stm32's toolchain is) and leaves
  `build/widgets.bin` and `build/scope.bin`, the same merged images. Docker or Podman is the only
  requirement; nothing on the host knows ESP-IDF. Developers who already have ESP-IDF can run
  `idf.py build` directly.
- **Flash from the host, not the container.** USB does not reach a container on macOS, so
  flashing is always the host's step (the web flasher, or `esptool.py`), never `idf.py flash`
  inside Docker. `scripts/flash.sh` wraps `esptool.py` and finds the board's port itself.
- **Read the console with anything.** UART0 at 115200 for logs (any serial terminal, or the web
  flasher's console). The profiler's dump shares the port at 921600: `prism-trace` switches
  the speed itself.
- **The README is the manual:** what the board is, which image does what, flash, see the console,
  take a trace; screenshots of both apps. Every error the board can print on the console says
  what to do next (e.g. "no PSRAM found: is this a WROOM-3 module?").
- **An app is a model and a view**, as on the desktop: the README shows the one place a new app
  is added (`main/`), and the same app source builds for the desktop and the STM32.

## Risks

- **Frame rate.** 800×480 is 2.9× the STM32's pixels, drawn into PSRAM. The STM32 spends
  75 ms of a ~90 ms frame redrawing every pixel; expect a few frames a second here until PRISM
  redraws only what changed (a PRISM change the STM32 needs too). First measure, with the
  profiler.
- **PSRAM bandwidth.** The RGB panel reads its frame from PSRAM continuously while the CPU and
  the PPA write; on the ESP32-S3 this starves the panel (the picture shifts). ESP-IDF's
  remedies: bounce buffers in SRAM, a lower pixel clock.
- **Young SDK.** ESP-IDF master and a preview chip: drivers may change under us. Pin the
  ESP-IDF commit in the repository.

## Testing

No emulator: every board step ends with the user flashing the board (`idf.py flash monitor`
on the USB-UART port) and reading the console. So each step prints what it proves (a line per
check, `PASS`/`FAIL`, as the QEMU firmwares do), and a failure prints enough to diagnose
without a debugger. Host-side parts (the shared apps, the build option) keep their PRISM tests.

## Order

1. Shared apps into PRISM; prism-stm32 uses them (no behaviour change; QEMU and the board build
   still pass).
2. prism-esp32 skeleton: ESP-IDF project, PRISM built through Meson, `app_main` prints
   PRISM's snapshot of a test model on the console (proves build and threads).
3. Display and touch: the widgets demo on the panel.
4. Microphones: the audio scope.
5. Profiler: clock, UART dump, a trace from the board.
6. A CI job: build only (an ESP-IDF container), no hardware.

## Left to the builder

- The cross-file generation, and which ESP-IDF flags PRISM must share.
- The PPA call sequence, and buffer placement (alignment, cache sync).
- Thread priorities and core pinning (logic, render, microphone).
- The ESP-IDF commit to pin.
