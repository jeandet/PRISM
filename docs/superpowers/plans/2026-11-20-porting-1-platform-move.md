# Porting PRISM, step 1: the platform code moves into PRISM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything in prism-stm32 that does not depend on the board (the frame runtime, the
rasterizer, FrameStats, the ring, the FreeRTOS POSIX layer, the QEMU test board) lives in
PRISM, is tested by PRISM's CI on a QEMU Cortex-M7, and prism-stm32 keeps only `boards/` and its
apps, written as model + view.

**Architecture:** PRISM gains a `sdl` build option, a `prism-raster` library
(`prism::raster`), a frame runtime (`prism::platform::frame_app(sink, touch, model, setup)`,
the board counterpart of `model_app`), and `ports/freertos/` (sources a board compiles against
its own `FreeRTOSConfig.h`). A cross build of PRISM (`host_machine.system() == 'none'`) also
builds the QEMU MPS2-AN500 test firmwares and runs them with `meson test`. prism-stm32 consumes
PRISM with `subproject('prism')`.

**Tech Stack:** C++26 (GCC 16 host, threaded arm-none-eabi GCC 15.2 board), meson, doctest,
FreeRTOS-Kernel, stb_truetype, QEMU `mps2-an500`, GitHub Actions + ghcr.io.

**Spec:** `doc/design/porting-and-devices.md` (PRISM), section 5 step 1, and sections 1–2.

## Global Constraints

- PRISM style: AGENTS.md (headers keep templates, `src/` the rest; no bare `detail` namespace;
  `simplify:` on deliberate cuts; strong geometry types).
- Builds and tests on Coder only, one at a time, foreground: PRISM with
  `/tmp/claude-6516/prism_remote.sh '<cmd>'`, prism-stm32 with `scripts/remote.sh '<cmd>'`.
  Cross builds of PRISM on Coder: `~/opt/arm-gcc-threads/bin` on `PATH`.
- No regression on the board: the timing line's allocations per frame stay ≤ 20 (part 1 of
  allocation-free frames), Renode's scope screenshot looks the same, flash ≤ 1170 KB.
- Commits with explicit pathspecs, one per task per repository; nothing pushed without the user
  saying so. Every commit message ends with the Co-Authored-By line.
- Headers touched in `include/prism/` also pass a syntax check without reflection and with
  clang + libc++ (memory: check-no-reflection-build).
- Rulings for this step (from the spec, decided while planning):
  - `FrameSink` and `TouchSource` stay virtual, as in prism-stm32 today. The `Framebuffer`
    concept, pixel formats and accelerated operations are step 3.
  - The audio scope, its spectrum, the synthetic source and the ADC conversion stay in
    prism-stm32: they move with the sample-stream family (step 2).
  - The timing line reaches the app through `FrameContext::on_stats(callback)`: the app puts
    it in a field of its own (the spec's "a field the runtime offers to show", in its simplest
    form).
  - `frame_app` returns after `FrameContext::quit()` (new: host tests and the desktop
    emulation need a runtime that ends).
  - The development image stays in prism-stm32 (`docker/Dockerfile`) and is published to
    `ghcr.io/jeandet/prism-stm32-dev`, which PRISM's CI job runs in.

## Review Focus

- PRISM configured as a subproject with a bare-metal cross file and `-Dsdl=disabled` touches no
  SDL, Python or host-only dependency. → Task 1 test (cross configure).
- A model built on the board's app task is used only from the logic thread; the setup hook and
  `on_loop` hooks run there. → Task 3 test.
- `quit()` while the render thread is waiting in `present()` or for a snapshot: `frame_app`
  returns, nothing deadlocks. → Task 3 test.
- The POSIX layer is compiled once per `FreeRTOSConfig.h` (QEMU's and the board's differ):
  PRISM hands out source files, not a prebuilt library. → Tasks 4 and 7 (both firmwares link).
- A port's linker script must define `end` and `_heap_end` for `system_counters()`. → Task 4:
  QEMU test 16 reads the counters (heap total > 0, idle time grows).

---

### Task 1: PRISM builds without SDL (`-Dsdl=disabled`)

**Files:**
- Modify: `meson_options.txt`, `meson.build`, `src/meson.build`
- Create: `tests/cross/none-cortex-m7.ini` (a cross file for configure-only checks, see step 1)

**Interfaces:**
- Produces: option `sdl` (feature, default `auto`); when disabled, `src/meson.build` builds
  `prism_lib`/`prism_core_dep` only, and the top level skips `python`, `tests`, `examples`,
  `benchmarks`. Variable `prism_core_dep` stays the name a parent uses.

- [ ] **Step 1: The failing check.** On Coder:
  `meson setup build-nosdl -Dsdl=disabled && ninja -C build-nosdl` — Expected now: meson errors
  (unknown option `sdl`). Then a configure-only cross check with
  `tests/cross/none-cortex-m7.ini` (copy of prism-stm32's `cross/stm32h7.ini`, compilers
  `arm-none-eabi-gcc/g++` through prism-stm32's `-pthread`-stripping wrappers moved to
  `scripts/arm-gcc`, `scripts/arm-g++`): `meson setup build-arm --cross-file
  tests/cross/none-cortex-m7.ini -Dsdl=disabled` — Expected now: fails resolving SDL.
- [ ] **Step 2: Implement** the option. `has_reflection` detection, wraps and
  `prism_core_dep` unchanged; everything SDL (software backend, sdl3/ttf/image, harfbuzz,
  webgpu) inside `if get_option('sdl').allowed()`; `subdir('python'/'tests'/'examples'/
  'benchmarks')` also require it. `auto` means enabled on a non-`none` host.
- [ ] **Step 3: Verify:** both commands of step 1 succeed; `ninja -C build-nosdl` builds
  `libprism`; `ls build-arm/subprojects` has no `SDL3*`; the normal `builddir` suite still
  passes (`meson test -C builddir`, 185/185).
- [ ] **Step 4: Commit** (PRISM): `build: -Dsdl=disabled builds libprism alone`.

### Task 2: `prism::raster` — the software rasterizer in PRISM

**Files:**
- Create from prism-stm32 (`raster/` → here, namespace `raster` → `prism::raster`):
  `include/prism/raster/{canvas,fonts,draw_snapshot}.hpp`, `src/raster/{canvas,fonts,draw_snapshot}.cpp`
- Create: `subprojects/stb.wrap` (wrap-git `https://github.com/nothings/stb`, revision
  `6b5799bae8a1fe3792691ef4b72ae26434c3e799`, `patch_directory = stb`),
  `subprojects/packagefiles/stb/meson.build` (`stb_dep`: include dir only)
- Create: `assets/fonts/embedded/NotoSans-Latin.ttf`, `NotoSans-Bold-Latin.ttf` (from
  prism-stm32 `assets/`), `scripts/embed.py` (from prism-stm32)
- Modify: `src/meson.build` (library `prism-raster`, static, built with or without SDL),
  `tests/meson.build`
- Test: move prism-stm32's `host/tests/test_canvas.cpp`, `test_fonts.cpp`, `test_snapshot.cpp`,
  `test_raster_allocations.cpp` to `tests/test_raster_{canvas,fonts,snapshot,allocations}.cpp`

**Interfaces:**
- Produces: `prism_raster_dep`; `prism::raster::Canvas`, `prism::raster::Fonts`
  (a `TextMeasurer`), `prism::raster::draw_snapshot(Canvas&, const SceneSnapshot&, Fonts&)` —
  signatures as in prism-stm32 today, namespace changed. `#include "third_party/stb/stb_truetype.h"`
  becomes `#include <stb_truetype.h>`.

- [ ] **Step 1: Move the tests first**, registered in `tests/meson.build` (headless, linking
  `prism_raster_dep`; `test_raster_allocations` like `alloc_free`: outside `headless_tests`,
  skipped on Windows); build — Expected: fails, `prism/raster/canvas.hpp` not found.
- [ ] **Step 2: Move the sources and the wrap**; `-Dsdl=disabled` builds `prism-raster` too.
- [ ] **Step 3: Verify:** the four tests pass, full suite green, no-reflection and clang
  syntax checks of the three headers.
- [ ] **Step 4: Commit** (PRISM): `feat(raster): the software rasterizer from prism-stm32`.

### Task 3: `prism::platform` — the frame runtime, FrameStats and the ring

**Files:**
- Create from prism-stm32 (`app/runtime.*`, `app/frame_stats.*`, `app/spsc_ring.hpp`,
  `app/alloc_count.hpp`): `include/prism/platform/{frame_app,frame_target,frame_stats,alloc_count}.hpp`,
  `include/prism/dev/spsc_ring.hpp`, `src/platform/{frame_runtime,frame_stats}.cpp`
- Modify: `src/meson.build` (library `prism-platform`, depends on `prism_core_dep` and
  `prism_raster_dep`), `tests/meson.build`
- Test: `tests/test_frame_app.cpp` (new), `tests/test_frame_stats.cpp`, `tests/test_spsc_ring.cpp`
  (moved from prism-stm32 `host/tests/`)

**Interfaces:**
- Produces: `prism_platform_dep` (meson). Namespace `prism::platform`:
  - `struct FrameSink`, `struct TouchSource` — as prism-stm32's `app::FrameSink`/`TouchSource`.
  - `class FrameContext { void on_loop(std::function<void()>); void on_stats(std::function<void(std::string_view)>); void quit(); }`
    — given to `setup` on the logic thread; `on_loop` hooks run each logic loop before the
    dirty check; `on_stats` receives each timing line.
  - `template <class Model> void frame_app(FrameSink&, TouchSource&, Model&, std::function<void(FrameContext&)> setup = {})`
    — builds the `WidgetTree` from the model on the logic thread, then runs prism-stm32's
    runtime (4 snapshot slots, render thread, touch routing, `FrameStats`); returns after
    `quit()`. The template only wraps the tree construction in a
    `std::function<std::unique_ptr<app::WidgetTree>()>` passed to the non-template
    `run_frames(FrameSink&, TouchSource&, TreeFactory, std::function<void(FrameContext&)>)` in
    `src/platform/frame_runtime.cpp`.
  - `std::uint32_t allocation_count()`, `std::optional<SystemCounters> system_counters()` —
    declared in `platform/alloc_count.hpp` and `frame_stats.hpp`, defined by the port
    (Task 4) or by the test executable.
  - `prism::dev::SpscRing<T, N>` — prism-stm32's ring, namespace changed.
- `FrameStats`, `SystemCounters`, `StageTimes` — prism-stm32's, namespace changed.

- [ ] **Step 1: Tests.** Move test_frame_stats and test_spsc_ring. New `tests/test_frame_app.cpp`
  with a memory `FrameSink` (a `std::vector<uint32_t>` 120×80, counts presents) and a scripted
  `TouchSource`, and a model `{ Field<Button> plus{{"+1"}}; Field<int> count{0}; }` with a view:
  - "frame_app presents a first frame and returns after quit": setup registers an `on_loop`
    that calls `quit()` once `presents ≥ 1`; `frame_app` returns; `presents ≥ 1`.
  - "a tap on the button reaches the model on the logic thread": the touch source reports the
    button's centre (from the first snapshot's geometry) then nothing; an `on_change` on
    `plus` records `std::this_thread::get_id()`; quit when `count == 1`; that id differs from
    the test thread's.
  - "quit while the render thread waits ends the run": `present()` blocks 50 ms each time;
    quit from setup's first `on_loop`; `frame_app` returns within 1 s.
  - "the timing line reaches on_stats": quit on the first line; it starts with a digit and
    contains "fps".
  The test executable defines `allocation_count()` (counting `operator new`, as
  `tests/alloc_counter.hpp`) and `system_counters()` returning `std::nullopt`.
- [ ] **Step 2: Run; fails** (headers missing).
- [ ] **Step 3: Implement** by moving the code; `run_frames` gets a `std::stop_source` checked
  by both threads' loops and the render thread's wait; `Example` and `ExampleFactory` are
  gone (their `tick`/`show_stats` become `on_loop`/`on_stats`).
- [ ] **Step 4: Verify:** the new and moved tests pass under `meson test` (and the frame_app
  tests 20 times with `--repeat 20`: no hang), suite green, header syntax checks.
- [ ] **Step 5: Commit** (PRISM): `feat(platform): frame_app, the runtime for framebuffer targets`.

### Task 4: `ports/freertos` and the QEMU test board in PRISM

**Files:**
- Create from prism-stm32: `ports/freertos/` ← `posix/*` (all ten files) +
  `boards/stm32h7b3i_dk/system_counters.cpp` (namespace → `prism::platform`, it is
  FreeRTOS + newlib, not board code) + `boards/stm32h7b3i_dk/sbrk.c`;
  `ports/qemu_mps2/` ← `boards/qemu_mps2/*`; `tests/embedded/posix_qemu/main.cpp`,
  `tests/embedded/prism_qemu/main.cpp` ← `tests/*_qemu/main.cpp`
- Create: `ports/meson.build`, `ports/freertos/meson.build`, `subprojects/freertos-kernel.wrap`
  (wrap-git `https://github.com/FreeRTOS/FreeRTOS-Kernel`, revision
  `83a45bf290024de16ca94df1f7262bcde6c82ffe`, `patch_directory = freertos-kernel`),
  `subprojects/packagefiles/freertos-kernel/meson.build`
- Modify: `meson.build` (`if host_machine.system() == 'none'`: `subdir('ports')`)

**Interfaces:**
- Produces (meson variables a parent reads with `subproject('prism').get_variable(...)`):
  `freertos_kernel_files` (tasks/queue/list/timers/event_groups + `heap_4.c`),
  `freertos_kernel_inc` (its `include/`), `freertos_cm7_port_files`, `freertos_cm7_port_inc`
  (`portable/GCC/ARM_CM7/r0p1`), `freertos_posix_files`, `freertos_posix_inc`,
  `alloc_count_link_arg` (`-Wl,--wrap=_malloc_r`). Each board compiles them with its own
  `FreeRTOSConfig.h` include directory.
- The port defines `prism::platform::allocation_count()` (over `prism_allocation_count()`)
  and `system_counters()`.

- [ ] **Step 1: Test 16** in `tests/embedded/posix_qemu/main.cpp`: `system_counters()` has a
  value, `heap_total > 0`, `stacks_total == configTOTAL_HEAP_SIZE`, and `idle_us` grows across
  a 100 ms sleep. Cross build on Coder
  (`meson setup build-arm --cross-file tests/cross/none-cortex-m7.ini -Dsdl=disabled`)
  — Expected: fails, `ports/` does not exist yet.
- [ ] **Step 2: Move** the files and write the two meson files; the QEMU firmwares
  `posix_tests.elf` and `prism_tests.elf` link `prism_core_dep`, `prism_raster_dep`, the
  kernel and the posix files compiled with `ports/qemu_mps2` as config dir;
  `ports/qemu_mps2/mps2_an500.ld` defines `_heap_end` if it does not yet. Each is a meson
  test: `test('posix_qemu', qemu_system_arm, args : ['-M', 'mps2-an500', '-nographic',
  '-semihosting-config', 'enable=on,target=native', '-kernel', posix_tests], timeout : 120)`.
- [ ] **Step 3: Verify:** `meson test -C build-arm` — posix_qemu 16/16, prism_qemu passes;
  the host `builddir` suite still green.
- [ ] **Step 4: Commit** (PRISM): `feat(ports): FreeRTOS POSIX layer and the QEMU Cortex-M7 test board`.

### Task 5: The development image on ghcr.io (prism-stm32)

**Files:**
- Create: `.github/workflows/image.yml` (prism-stm32)
- Modify: `README.md` (prism-stm32, "Development image")

**Interfaces:**
- Produces: `ghcr.io/jeandet/prism-stm32-dev:<tag>` (tag `arm-YYYYMMDD` and `latest`), built
  from `docker/Dockerfile` with the default Fedora 44 base, on pushes touching `docker/**` or
  `scripts/build-toolchain.sh`, and on `workflow_dispatch`.

- [ ] **Step 1: Write the workflow** (`docker/build-push-action` with `permissions: packages:
  write`, GHA layer cache so the GCC stage is rebuilt only when the script changes).
- [ ] **Step 2: Verify locally** what CI cannot: `actionlint` (or `yamllint`) on the file.
- [ ] **Step 3: Commit** (prism-stm32). The first run needs a push and a manual dispatch:
  ask the user; record the resulting tag for Task 6.

### Task 6: PRISM CI cross-compiles and runs the QEMU tests

**Files:**
- Modify: `.github/workflows/ci.yml` (PRISM)

**Interfaces:**
- Consumes: Task 5's image tag; Task 4's cross build and tests.

- [ ] **Step 1: Add a job `embedded-qemu`**: `container: ghcr.io/jeandet/prism-stm32-dev:<tag>`,
  `meson setup build-arm --cross-file tests/cross/none-cortex-m7.ini -Dsdl=disabled`,
  `ninja -C build-arm`, `meson test -C build-arm`, upload `meson-logs/testlog.txt` on failure;
  plus `size` of `prism_tests.elf` in the log.
- [ ] **Step 2: Verify locally:** the same commands inside the image on the build host
  (`podman --remote run` with the repository mounted) pass.
- [ ] **Step 3: Commit** (PRISM): `ci: embedded job, PRISM cross-compiled and tested on QEMU Cortex-M7`.

### Task 7: prism-stm32's host build and apps on PRISM

**Files (prism-stm32):**
- Create: `subprojects/prism.wrap` (wrap-git `https://github.com/jeandet/PRISM`, revision
  `main`; on Coder `subprojects/prism` is a symlink to `~/PRSIM`, created once and excluded
  from `scripts/remote.sh`'s rsync)
- Modify: `host/meson.build`, `host/tests/test_audio_scope.cpp`, `host/scope_bench.cpp`,
  `host/alloc_sites.cpp`, `app/audio_scope.{hpp,cpp}`, `app/widgets_example.hpp` →
  `app/widgets_app.hpp`, `scripts/remote.sh`
- Delete: `host/tests/test_{canvas,fonts,snapshot,frame_stats,spsc_ring,raster_allocations}.cpp`
  and `host/render_demo.cpp` (moved to PRISM in Tasks 2–3)

**Interfaces:**
- Consumes: `prism_core_dep`, `prism_raster_dep`, `prism_platform_dep` from
  `subproject('prism', default_options : ['sdl=disabled'])`; `FrameContext` (Task 3).
- Produces (namespace `app`): the scope as model + functions, no `Example`:
  `struct ScopeModel` (its fields, as today, with a `view`), `class ScopeState` (window, FFT,
  buffers, readout counters: what `AudioScope` holds besides the model; constructed from a
  `SampleSource&`), `void scope_tick(ScopeModel&, ScopeState&)`,
  `void scope_show_stats(ScopeModel&, ScopeState&, std::string_view)`, and
  `void attach_scope(prism::platform::FrameContext&, ScopeModel&, ScopeState&)` registering
  the two as `on_loop` and `on_stats`. The widget demo: `struct WidgetsModel` with a `view`
  and `attach_widgets(FrameContext&, WidgetsModel&)` (stats only).

- [ ] **Step 1: Tests first.** Rewrite `test_audio_scope.cpp` against `ScopeModel` +
  `ScopeState` + `scope_tick`/`scope_show_stats` (a `WidgetTree` built from the model for the
  snapshot test), keeping every existing assertion. Switch `host/meson.build` to the
  subproject. Build — Expected: fails, `ScopeState` not declared.
- [ ] **Step 2: Implement** the scope and widget apps in that shape; scope_bench and
  alloc_sites drive `scope_tick` + `WidgetTree::build_snapshot` as they drive `tick()` today.
- [ ] **Step 3: Verify:** host suite green (spectrum, synthetic source, ADC sample, audio
  scope); `scope_bench` prints its timing line with ≤ 19 allocations per frame.
- [ ] **Step 4: Commit** (prism-stm32): `host, apps: build on PRISM; apps as model + view`.

### Task 8: prism-stm32's firmware on PRISM

**Files (prism-stm32):**
- Modify: `meson.build`, `meson.options` (drop `prism_dir`, `prism_sources`),
  `boards/stm32h7b3i_dk/main.cpp`, `README.md`
- Delete: `app/runtime.*`, `app/frame_stats.*`, `app/spsc_ring.hpp`, `app/alloc_count.hpp`,
  `app/example.hpp`, `raster/`, `posix/`, `boards/qemu_mps2/`, `tests/`,
  `boards/stm32h7b3i_dk/{system_counters.cpp,sbrk.c}`, `scripts/embed.py`, `assets/`,
  submodules `third_party/FreeRTOS-Kernel` and `third_party/stb`

**Interfaces:**
- Consumes: `frame_app` (Task 3), PRISM's meson variables (Task 4), Task 7's apps.
  `main.cpp`: `frame_app(display, touch, model, [&](FrameContext& ctx) { attach_scope(ctx, model, state); })`.

- [ ] **Step 1: The board's acceptance check, before changing anything:** build, Renode
  screenshot of the scope, and `tap.resc` with `-Dexample=widgets` (counter reaches 2). Keep
  the screenshot to compare.
- [ ] **Step 2: Switch the firmware build** to PRISM's library and variables (the FreeRTOS
  kernel and POSIX files compiled with the board's `FreeRTOSConfig.h`); delete the moved
  files; `main.cpp` calls `frame_app`.
- [ ] **Step 3: Verify:** the firmware builds; the Renode scope screenshot matches the kept
  one apart from the numbers; the widgets tap counts to 2; the timing line shows ≤ 20
  allocs/frame; `stm32h7b3i_dk.size.txt` ≤ 1170 KB of flash.
- [ ] **Step 4: Commit** (prism-stm32): `build: the firmware on PRISM's platform and ports`.
