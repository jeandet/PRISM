# ESP32-P4 Port (and S3)

Design note for plan item 20 of `doc/review-2026-11-13.md`. Date: 2026-11-15.

## Overview

The goal is PRISM on a board with a real panel: a P4 first, maybe an S3 later. Not a demo that
draws a rectangle. A screen with the same model/view split as the desktop, driven by the same
widget tree.

The port is mostly a display backend plus a memory diet. The architecture already assumes the worst
case in two places: the web build proves the no-reflection path, and `BackendBase` already asks only
for `submit(snapshot)`, `wake()` and `run(event_cb)`.

## The target hardware

Facts from the vendor's product pages, checked 2026-11-15.

ESP32-P4 (https://www.espressif.com/en/products/socs/esp32-p4):

- Dual-core RISC-V, up to 400 MHz, single-precision FPU, AI extensions. A low-power core up to
  40 MHz.
- 768 KB of on-chip SRAM in the high-performance system, plus 8 KB of zero-wait TCM. The SRAM can be
  used as cache when external PSRAM is present.
- MIPI-DSI and a parallel RGB interface, up to 1080p. MIPI-CSI for cameras.
- Hardware PPA (pixel processing accelerator) and 2D-DMA.
- 55 GPIOs, UART, I2C, SPI, I2S, LEDC, MCPWM, RMT, ADC, TWAI, USB OTG 2.0 HS, Ethernet, SDIO.
- Capacitive touch inputs.
- No Wi-Fi and no Bluetooth on the chip. Radio comes from a companion ESP32-C or -S over SPI, SDIO
  or UART, through ESP-Hosted or ESP-AT.

ESP32-S3 (https://www.espressif.com/en/products/socs/esp32-s3):

- Dual-core Xtensa LX7 at 240 MHz, 512 KB of internal SRAM.
- Wi-Fi 2.4 GHz and Bluetooth 5 LE on chip. 45 GPIOs, 14 of them capacitive touch.
- Octal SPI flash and PSRAM support.

The difference that matters for a port: the P4 has the display interfaces and the compute, the S3
has the radio and less RAM. Neither has the other's strength.

## What a screen costs here

Arithmetic this note did, not a vendor claim. RGB565 is two bytes per pixel.

| Panel | Frame, RGB565 | Two frames | Fits in 768 KB SRAM |
|---|---|---|---|
| 480×320 | 300 KB | 600 KB | barely, and nothing else |
| 800×480 | 750 KB | 1.5 MB | no |
| 1024×600 | 1.2 MB | 2.4 MB | no |
| 1920×1080 | 4.1 MB | 8.3 MB | no |

So a P4 port at any usable resolution is either strip-buffered — rasterise N lines, flush, repeat —
or it renders into PSRAM. Strip buffering is what esp_lcd is built around: the driver takes a
rectangular window and a pixel buffer for it.

The 400 MHz dual-core budget is roughly a thousandth of a desktop core per cycle, per pixel, if you
want 60 Hz on a full 800×480 panel. That rules out the desktop strategy of repainting everything on
every present. PRISM's dirty-widget repaint is the reason this is even a conversation; the port has
to actually use it.

## Toolchain

Facts from ESP-IDF v6.1.1 documentation.

- Compiler: GCC 15.2.0 for both `riscv32-esp-elf` and `xtensa-esp-elf`, plus an optional clang-esp
  21.1.3
  (https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/tools/idf-tools.html).
  Upstream Clang does not implement P2996. Whether clang-esp does is unchecked.
- **Consequence: no `-freflection`.** PRISM's `has_reflection` probe fails, which is the same path
  the web build already takes.
- Default language level: `-std=gnu++26`
  (https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/cplusplus.html). The
  standard is not the problem; reflection is.
- C++ exceptions are off by default (`CONFIG_COMPILER_CXX_EXCEPTIONS`). RTTI is off by default
  (`CONFIG_COMPILER_CXX_RTTI`).
- `std::thread`, `std::mutex`, `std::condition_variable` exist and map onto pthreads, which wrap
  FreeRTOS tasks
  (https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/pthread.html).
- `std::jthread`'s destructor is only safe when called from a task created through the pthread or
  C++ threading API, not from the `app_main` task.
- Not implemented: `pthread_cancel()` and `pthread_condattr_init()`, both returning `ENOSYS`.
  `pthread_cond_timedwait` resolution is the RTOS tick.
- `app_main` must be `extern "C"`. Designated initializers have restrictions.

## Build: a Meson project, consumed by CMake

ESP-IDF has no Meson backend. It is CMake plus `idf.py`, per component
(https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/build-system.html).

Three ways to put a Meson project on the board. They are not equally good.

1. **Meson cross-build to a static library, ESP-IDF imports it.** Meson gets a
   `--cross-file` naming `riscv32-esp-elf-g++`, builds `libprism_core.a`, and stops. The IDF
   component declares `add_library(prism_core STATIC IMPORTED)` and points at the archive. IDF
   documents this as "Using Prebuilt Libraries with Components" and "Using ESP-IDF Components from
   External Libraries".
2. **IDF as a Meson subproject.** Does not work. There is nothing for Meson to drive.
3. **A `ports/esp32/` directory with its own CMakeLists.** Same as (1), with the CMake kept in this
   repo where it can be reviewed alongside the code it builds.

Recommendation: (1), with the small CMake shim in `ports/esp32/` so it is versioned with PRISM. The
cross file is the only file Meson needs to learn a new target, which is why the split is cheap.

The other half of the same problem: the preview loop is terrible on real hardware. Keep a host build
that runs the same widget tree, and make the board the second target, not the first.

## What PRISM has, and what is missing

### Reflection

Already handled. Without `-freflection`, model structs need a hand-written `view()` instead of
getting one synthesized. `src/meson.build` already conditions magic_enum on the probe, and most
examples and tests are written to build both ways.

Cost: every struct on the board needs its `view()` written by hand. For a device UI that is a
feature, not a tax — a phone-sized screen has fewer widgets than a desktop app, and hand-written
views are easier to clamp.

### Display backend

Today's state, from this tree:

- `src/backends/software_backend.cpp` is 662 lines and includes SDL3 and SDL_ttf directly. It owns
  the window, the event loop, fonts, file dialogs, message boxes, clipboard and the system theme.
- `include/prism/render/software_renderer.hpp` is SDL-free and implements `render_frame(snapshot)`
  into a `PixelBuffer`. It is a proof of concept: `FilledRect` rasterises, and `TextCmd`,
  `ClipPush/ClipPop`, `RoundedRect`, `Line`, `Polyline`, `Circle`, `FilledPolygon` and `ImageCmd`
  are no-ops. Only `tests/test_software_renderer.cpp` uses it.
- `render-backend.md` calls that renderer "kept only for unit tests ... not on the production
  path". The port is what changes that.

Work, in dependency order:

1. Finish the rasteriser for every command in the Backend Contract. Rect outlines, lines and
   polygons first: they are all of the chrome and all of the plot frames. Rounded rects and circles
   next, since tessellation lives in `shape_mesh.hpp` and does not need a GPU. Text last.
2. A presenter that calls `esp_lcd_panel_draw_bitmap()` with a window, per strip
   (https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/index.html).
   The driver takes a 1-D array with no stride between lines; `_2d` takes explicit source and target
   windows.
3. A `BackendBase` implementation around those two, with `run()` as the frame loop and `submit()`
   storing one immutable snapshot, exactly as `threading-model.md` describes. No SDL anywhere.

The 662-line SDL file is not a template for this. It is a desktop backend, and half of it is
desktop-only features (dialogs, clipboard, theme) that the port should not link at all.

### Touch input

`InputEvent` and the hit-testing code already take pointer positions. A touch panel driver from the
Component Registry produces the same down/move/up events. Two shape differences to design for:

- No hover. Any affordance that only appears on hover must gain a press-based path, and the widgets
  that assume a mouse wheel need a drag fallback.
- One finger, usually. Multi-touch exists in hardware; do not design for it and do not rule it out.

### Fonts

The desktop path is FreeType plus HarfBuzz behind SDL3_ttf. HarfBuzz exists here for shaping:
ligatures, Arabic, Devanagari.

The port does not need a shaper. `include/prism/render/text_bitmap.hpp` already exists, so the
lighter path is a baked glyph atlas, generated at build time from the same TTFs that `TextMeasurer`
measures against. That keeps the desktop invariant — measured widths equal drawn widths — which is
what stops text from overflowing its box on one machine and not the other.

Cost: no complex-script shaping unless the atlas includes pre-shaped runs. For a device UI that is
acceptable. For a P4 driving an instrument panel, Latin plus symbols is the whole alphabet.

Whether FreeType alone (no HarfBuzz, no SDL3_ttf) is small enough to keep is unchecked. It would
preserve hinting and arbitrary sizes, at a size cost this note did not measure.

### Memory

Three allocations per frame matter on a device with 768 KB of SRAM.

1. The snapshot. `SceneSnapshot` holds a geometry vector, a `shared_ptr<const DrawList>` per widget,
   a z-order vector and the overlay. For a 200-widget screen this is the big one, and it is
   allocated fresh on every publish today.
2. `resolve_clips()` and layout results, which produce more vectors.
3. The draw lists themselves: each dirty widget rebuilds its own.

The upgrade is one arena per frame, bump-allocated, with the snapshot owning it. Two frames in
flight means two arenas. That removes nearly all of the per-frame churn without changing a single
public type — `SceneSnapshot` already hands around shared, immutable data.

Until that exists, the port's answer is a smaller screen: 480×320 with 60 widgets is a very
different memory problem from a desktop window with 300.

### Threads

The desktop model is a logic thread that owns the widget tree and every `Field`, and a render thread
that owns the backend and never calls app code. The invariants in `threading-model.md` — no mutex
across the frame boundary, no app code on the render thread, immutable data across the boundary —
are what makes that safe, and they cost nothing on a two-core RISC-V part.

Recommendation: keep the two threads and pin them, logic to one core and render to the other, using
`esp_pthread_set_cfg()`. Do not collapse to one thread to save RAM: the two-thread split is the
architecture, and the port would be testing a different program.

Two toolchain traps, both already listed above: `std::jthread`'s destructor off `app_main`, and
`pthread_condattr_init()` returning `ENOSYS`. Check that the timer and waker paths in this codebase
do not depend on either.

## Peripherals are item 19's seams

`doc/design/integrations.md` defines the crossing and the concepts. On this board:

| Peripheral | Seam | Notes |
|---|---|---|
| UART | `ByteStream` | ESP-IDF UART driver, wrapped as an adapter. The instrument case of item 19, on a wire. |
| Wi-Fi | sockets | lwIP's BSD socket API. Whether Asio builds on it is unchecked. |
| MQTT | `Channel<T>` | `esp-mqtt` is a component; whether Paho C is packaged for IDF is unchecked. |
| I2C, SPI | none of them | Register transactions, not byte streams. Keep out of the concept; wrap per device. |
| BLE | request/response | GATT is closer to `Requester` than to `ByteStream`. NimBLE is the component. |

The important consequence: the board does not need a different I/O design. It needs a second
adapter per seam, and the app keeps writing `telemetry_ingest.set(...)`.

## Step plan

**Milestone 0 — toolchain, no UI.** A Meson cross file for `riscv32-esp-elf`. `libprism_core.a`
built and linked into an IDF component. It boots, logs its version and the size of `Field<int>`.
Nothing else. If this is not clean, nothing after it is.

**Milestone 1 — one static screen.** Finish the rasteriser for `FilledRect`, `TextCmd` and `Line`.
Presenter over `esp_lcd` with a strip buffer. One hard-coded model with two fields, no input, no
threads beyond the backend's own loop. Success: it renders, and it renders the same pixels as the
host build's software renderer for the same snapshot. That last check is a test, not a screenshot
comparison — `tests/test_pixels.cpp` already works this way on the desktop.

**Milestone 2 — input.** A touch driver, `InputEvent` conversion, and the existing event routing.
Success: a button in the model toggles a field, and the field's label changes. This is the first
milestone where the port runs actual PRISM code rather than a slice of it.

**Milestone 3 — a real app.** Take `examples/model_lab_bench/`'s model, which is pure logic with no
I/O, and give it a `view()` for a small panel. Simulated link first. Success: a scope trace, a
readout and two buttons on 480×320, with the hardware thread stalling on demand without freezing
the UI — the same test the desktop example already proves.

**Milestone 4 — peripherals.** UART link adapter, then Wi-Fi and MQTT. Then the thin client of plan
item 17 becomes interesting: run the app headless on the board and draw it in a browser, which
avoids the panel, the fonts and the memory budget all at once.

## Risks

- **PSRAM and DMA.** `esp_lcd_panel_draw_bitmap()` needs a buffer the panel's DMA can read. Whether
  a PSRAM framebuffer is DMA-able on the P4 is unchecked, and it decides between a PSRAM
  framebuffer and strict strip buffering. Check this before writing the presenter, not after.
- **Exceptions off.** Whether the dependency set (fmt, Glaze, stdexec, sheenbidi) builds with
  `-fno-exceptions` is unchecked. fmt and stdexec both have throw paths in normal use.
- **32-bit.** ESP32-S3 is 32-bit. `cpp-httplib` explicitly refuses 32-bit platforms, so that HTTP
  path is closed there. PRISM's own `SampleRing` asserts `std::atomic<float>::is_always_lock_free`,
  which is unchecked on RV32 without the atomics extension in use.
- **Dirty repaint underused.** If the port repaints the whole panel every frame, 400 MHz will not
  get to 60 Hz on anything above 480×320. The instrumentation fields in `SceneSnapshot`
  (`dirty_widget_count`, `draw_command_count`, `approx_bytes`) already exist to catch that.
- **Two targets, one tree.** A `ports/esp32/` directory with its own CMake is a second build system
  in the repo. It should stay a shim that compiles and links, never a place where code lives.

## Open questions

1. Panel and board: resolution, PSRAM size and touch controller are board properties this note did
   not check. A 480×320 with 8 MB PSRAM is a different project from 1024×600 with 32 MB.
2. Do we bake fonts at build time (needs a host step in Meson) or ship a fixed atlas? Baking keeps
   `TextMeasurer` honest but adds a generator.
3. Is `-fno-exceptions` acceptable for the firmware backend only, or does the whole of
   `prism_core_dep` have to survive it?
4. Two threads pinned, or one thread and a synchronous present? This note recommends two, because
   it tests the real architecture. It is worth measuring before committing.
5. Does `ports/esp32/` live here or in a separate repo? Keeping it here keeps the cross file next to
   the code it builds, at the cost of a CMake file in a Meson project.
