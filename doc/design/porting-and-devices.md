# Porting PRISM, and devices

**Status:** Proposal — for review before code.

## Why

PRISM now runs on a microcontroller (the STM32H7B3I-DK, in the prism-stm32 repository). That
port was written in a hurry to see PRISM run: the runtime, the rasterizer and the POSIX layer it
needed live in the board repository, and the board code talks to ST's HAL directly. A second
target would copy most of it.

Two goals:

1. **A port writes only what the target owns.** Board bring-up (clocks, memory map, linker
   script), the display's memory and its "show this frame", the touch panel, the peripherals.
   Everything that does not depend on the hardware lives in PRISM and is shared: by
   microcontrollers, Linux boards with a screen, the desktop and the browser.
2. **A small number of device families,** so that algorithms and devices compose, the way Qt's
   QIODevice lets the same code read a file, a socket or a serial port. User code and libraries
   are written once against a family and run on any board, and on the desktop against an
   emulated device. The first user in sight is an MP3 player: an SD card, a decoder, a volume,
   I2S out, and a PRISM UI.

The shape of these interfaces is decided now, before more board code is written without one.

## Constraints

- **No cost in memory or speed.** Embedded and real-time developers are the audience
  (doc/design/allocation-free-frames.md): no allocation once started, no copy a device does not
  need, no indirect call per sample or per pixel.
- **DMA, in-place work and circular buffers stay possible** — and natural. An interface that
  forces a copy out of a DMA buffer, or a contiguous buffer where the device fills a ring, is
  wrong.
- **As simple as the devices allow.** Each family is a handful of member functions; anything
  that can be built on top of them is a free function or a small template, not a requirement.
- **Drawing scales** from a CPU rasterizing into a framebuffer in RAM up to any accelerator:
  the STM32's DMA2D (Chrom-ART), a 2D GPU (ST's NeoChrom/NemaGFX, NXP's VGLite), a desktop GPU.
- **Targets have an OS with threads** (FreeRTOS, Zephyr, Linux, desktop, browser workers). A
  build without threads is not a goal, and nothing here should make it impossible later.
- PRISM's style: C++26, concepts over virtual bases (doc/design/widgets-and-sentinels.md),
  strong geometry types, `simplify:` on deliberate cuts.

## Non-goals and anti-goals

- No register-level abstraction (Kvasir-style) and no device tree or code generation from chip
  data (modm, Zephyr): vendor HALs and SDKs do that, and a board's driver calls them.
- No RTOS of PRISM's own, and no new threading API: `std::thread`, `std::mutex`,
  `std::chrono` everywhere, through POSIX underneath.
- No runtime plugin loading.
- **Anti-goal:** an abstraction that makes the common case slower or bigger than calling the
  vendor HAL by hand. If the DMA2D fill through PRISM is slower than `HAL_DMA2D_Start`, the
  design failed.
- **Anti-goal:** a family per peripheral type (an "ADC interface", a "DAC interface", an "I2S
  interface"…). Families are about how data moves, not what the chip calls the block.

## Approaches considered

Every interface works on blocks (`std::span`) of samples, bytes or pixels, never on single
elements: the cost of a call is paid once per block. What differs is how a device is bound to
the code that uses it.

- **A. Concepts only, resolved at compile time.** Pipelines are template chains
  (`adc | gain(2.f) | fft<1024>() | plot`), everything inlines, buffers are sized at compile
  time. But choosing a device at runtime (the scope falls back to a generated signal when the
  ADC does not start; a settings menu picks an input) becomes awkward, error messages grow, and
  every combination is compiled separately (flash).
- **B. Virtual interfaces** (QIODevice, libhal). Simple and swappable, but every user pays a
  vtable and loses inlining between stages, even where nothing is ever swapped.
- **C. Concepts as the contract, plus an opt-in erased handle (chosen).** Code is written
  against concepts and compiles as in A. Where a choice is made at runtime, a non-owning handle
  (`any_source<float>`: an object pointer and a table pointer, no allocation, like
  `std::function_ref`) erases the type, at one indirect call per block. PRISM already works
  this way: widgets are concepts and templates, only the backend is virtual.

## Design

### 1. Who owns what

```
 PRISM                                         board repository (prism-stm32, …)
 ─────                                         ─────────────────────────────────
 core, widgets, layout, snapshots (as today)
 prism::platform                               boards/<board>/
   runtime for frame targets (logic + render)    startup, clocks, MPU, linker script
   software rasterizer (pixel formats)           its Framebuffer / Accel / Touch types
   FrameStats, allocation guard                  its device drivers (ADC, I2S, SD, …)
 prism::dev                                      main(): picks devices, calls run()
   family concepts + erased handles
   rings, block helpers, generic algorithms
   emulated devices (host, tests, Renode)
 ports/freertos (POSIX threads on FreeRTOS)    FreeRTOSConfig.h
```

Moves from prism-stm32 into PRISM: `app/runtime.*`, `app/frame_stats.*`, `app/spsc_ring.hpp`,
`app/example.hpp`, `raster/`, `posix/`, `app/adc_sample.hpp`, the synthetic source, and the
audio scope as an example. prism-stm32 keeps `boards/` and its build. PRISM gains a build
without SDL (`-Dsdl=disabled`), so a board repository uses it as a meson subproject.

### 2. Drawing: three levels a target plugs in at

A target implements the lowest level its hardware allows; PRISM does the rest.

```
 level 3  Renderer       consumes DrawLists itself      desktop SDL/WebGPU, NemaGFX, VGLite
 level 2  Accel ops      fills, blits, blends for the   DMA2D, PXP, a blitter
          + level 1      PRISM rasterizer
 level 1  Framebuffer    pixel memory + present         any screen with RAM behind it
 level 0  Band target    a strip of pixels + flush      SPI displays, no full framebuffer
```

**Level 1, `Framebuffer`** — what the STM32 port has today (`FrameSink`), as a concept:

```cpp
template <class T>
concept Framebuffer = requires(T& t) {
    typename T::pixel;                       // argb8888, rgb565, l8: a type, so the rasterizer
                                             // is compiled for the format, no per-pixel switch
    { t.back() } -> std::same_as<PixelView<typename T::pixel>>;  // data, size, stride
    { t.present() } -> std::same_as<void>;   // show back(); returns when back() is writable
};
```

**Level 0, band targets**, for a display behind SPI with no RAM for a whole frame (a 320×240
RGB565 panel is 150 KB; small chips have less): the rasterizer draws the frame in strips into a
buffer the target lends, and `flush(rect, pixels)` starts the transfer (DMA) and returns. Same
rasterizer, a different loop around it. *Our call:* each strip replays the whole snapshot,
skipping draw lists whose bounds miss it. A snapshot does not record which screen areas changed
(only how many widgets did), so drawing only changed areas is a separate PRISM change, for
later: it would help level 1 as much (the scope redraws all 480×272 pixels every frame, 75 ms
of its ~90).

**Level 2, accelerated operations.** The rasterizer decomposes everything into a few pixel
operations; a target may provide any of them, and the rasterizer uses the hardware for those
and the CPU for the rest. Each is optional, detected with a `requires` expression at compile
time, so a target implements exactly what its hardware does and pays nothing for the others:

| Operation | Used for | DMA2D |
|---|---|---|
| `fill(dst, rect, color)` | opaque rectangles, clears | register-to-memory |
| `blend_fill(dst, rect, color)` | translucent rectangles | memory-to-memory with blend, fixed colour |
| `blend_mask(dst, rect, a8 mask, color)` | anti-aliased text, icons | blend with A8 foreground |
| `blit(dst, rect, src view)` | images, cached layers, format conversion | memory-to-memory with PFC |
| `blend_image(dst, rect, src view)` | images with alpha | memory-to-memory with blend |

Shapes the hardware cannot do (polygons, rounded corners, lines, the edges of anti-aliased
shapes) stay on the CPU. Accelerators run asynchronously, in order: an operation is queued and
returns; `wait()` (also part of the accel concept) blocks until the queue is empty, and the
rasterizer calls it before the CPU touches pixels a queued operation may write, and before
`present()`. simplify: one fence for the whole queue, not per rectangle; per-rectangle tracking
when profiles show the CPU waiting.

**Level 3, renderers**, take the snapshot's DrawLists whole: the existing `BackendBase`
(SDL, WebGPU) and, later, a 2D GPU (NemaGFX on the STM32U5/H7R/S, VGLite on i.MX RT). The
design note render-backend.md keeps its reason for not forcing these through pixels.

The runtime above levels 0–2 is one generic piece of code: a logic thread that owns the model
and builds snapshots into reused slots, a render thread that draws and presents, input polled
from a touch/pointer source, the timing line. It is templated on the target's types, so a port
writes no runtime. *Our call:* the desktop's `model_app` stays as it is (level 3, SDL); the
desktop emulation of a board uses the frame runtime with an SDL window as its framebuffer.
Merging the two runtimes is later work, if ever.

**Apps are written as on the desktop (decided).** A board app is a model struct and its view,
exactly what `model_app` runs, so the same app source builds for the desktop and for a board.
The board runtime takes the model where `model_app` does; prism-stm32's `Example` base class
(`tree()`, `tick()`, `show_stats()`) goes. An app that polls devices each loop (the scope
draining its sample source) registers a per-loop hook on the runtime; the timing line is a
field the runtime offers to show, not a method every app implements.

### 3. Device families

Five families. Each is a concept; generic code takes `Family auto&`; `any_<family>` erases one
when a choice is made at runtime.

**Byte streams** — exists: `io::ByteStream` (`write`, `read_some`, `close`; blocking, on a
worker thread). UART, USB serial, TCP, a file, a file on an SD card. Unchanged.

**Sample streams** — continuous data at a fixed rate: ADC, microphone, audio in and out, a
decoder's output, a DSP stage. The source lends what it has; nothing is copied unless the
consumer wants a copy:

```cpp
struct StreamFormat { float rate; int channels; };   // interleaved frames

template <class S>
concept SampleSource = requires(S& s) {
    typename S::sample;                                   // int16_t, float, …
    { s.format() } -> std::same_as<StreamFormat>;
    { s.acquire() } -> std::same_as<std::span<const typename S::sample>>;  // empty: none yet
    { s.release(std::size_t{}) } -> std::same_as<void>;   // done with that many of it
};

template <class S>
concept SampleSink = requires(S& s) {
    typename S::sample;
    { s.format() } -> std::same_as<StreamFormat>;
    { s.acquire() } -> std::same_as<std::span<typename S::sample>>;        // space to fill
    { s.commit(std::size_t{}) } -> std::same_as<void>;    // that many are written
};
```

- **DMA, zero copy.** A DMA-backed ADC's `acquire()` returns the half of its circular DMA
  buffer that just completed (after the cache invalidation); `release(n)` hands it back. An I2S
  sink's `acquire()` returns the half the DMA is not reading. The interrupt only flips an index
  and signals.
- **Circular buffers.** `dev::Ring<T, N>` (the SPSC ring the scope uses, grown a lending API)
  is itself both a sink (for the producer) and a source (for the consumer), and returns
  contiguous regions, so a ring between an interrupt and a thread is the same family as the
  device.
- **In place.** A transform is a function over a mutable span (`gain(span<float>)`), applied to
  what a sink lent before committing it, or to a block acquired from a source: no buffer of its
  own. Transforms that change size or rate (decoder, resampler, FFT) are a source over a source.
- **Partial use.** `release(n)` gives back the first `n` samples of the last acquire; the
  rest are lent again by the next `acquire()`.
  A consumer that needs a longer window than a block (an FFT of 1024 over 256-sample
  blocks) keeps its own window, as the scope does: that copy is its algorithm's, not the
  interface's.
- **Overruns.** A source whose consumer falls behind drops the newest data and counts it
  (`overruns()`), as the scope's ring does today; it never blocks the interrupt and never hands
  out a half the DMA is writing.
- **Waiting.** `acquire()` never blocks. A source also gives `ready()`, a reference to a
  `dev::Signal`: a counting semaphore the interrupt (or the emulation thread) releases, which a
  thread waits on with a deadline. *Our call:* `Signal` is the one primitive implemented per
  port, because an interrupt cannot use the POSIX semaphore: on FreeRTOS `xSemaphoreGiveFromISR`
  (in `ports/freertos`), elsewhere `std::counting_semaphore`. simplify: blocking threads, not
  std::execution senders; senders when a single thread must wait on several devices at once.
- `read(source, span out)` and `write(sink, span in)`, copying, are free functions on top, for
  code that prefers a copy.

**Bus devices** — an I2C or SPI peripheral driver (a sensor, an audio codec, an SD card in SPI
mode). One transaction, blocking, buffers from the caller; DMA is the driver's business:

```cpp
template <class B>
concept I2cBus = requires(B& b, std::uint8_t address, std::span<const std::byte> out,
                          std::span<std::byte> in) {
    { b.transfer(address, out, in) } -> std::same_as<std::expected<void, std::error_code>>;
};

template <class D>
concept SpiDevice = requires(D& d, std::span<const std::byte> out, std::span<std::byte> in) {
    { d.transfer(out, in) } -> std::same_as<std::expected<void, std::error_code>>;  // CS held
};
```

That is Rust embedded-hal's success: drivers written once against these, tested against a
recorded or scripted bus.

**Lines** — a digital output (`set(bool)`), a digital input (`get()`), and edges as events: an
input with an interrupt gives a `dev::Signal` and a timestamp, so a button or an encoder feeds
PRISM's input queue or a `Channel<T>` like anything else.

**Pixel targets and input** — section 2, plus a pointer source (`read() → optional<Point>`, the
STM32 port's `TouchSource`) and keys.

**Not a family:** timers (a stream's rate is its format; a deadline is `std::chrono`), DMA itself
(always behind a stream or a bus), clocks and power (bring-up).

### 4. Emulation

The point of families: a device's emulation is one more type that satisfies the same concept,
in `prism::dev`, built for the host.

| Family | Emulated by |
|---|---|
| Sample source | a generator (the scope's sweep), a WAV file, the PC's microphone (SDL) |
| Sample sink | the PC's speakers (SDL, through `audio::Output`), a WAV file |

*Our call:* `audio::Output` keeps its API (it calls a render function on its own thread). Its
emulated sink is a `Ring`: the app commits into it, the render function acquires from it.
| Bus device | a scripted device model (registers in a map), or a recorded trace replayed |
| Lines | a PRISM panel: LEDs and buttons drawn as widgets |
| Framebuffer, touch | an SDL window and the mouse, at the board's size and pixel format |

The whole board firmware (its `main` aside) then runs on the desktop at the board's resolution,
with its pixel format and band size, before any hardware exists.

### 5. Order

1. **Move** the runtime, rasterizer, rings and POSIX layer into PRISM; `-Dsdl=disabled`; prism-stm32
   consumes PRISM as a subproject and keeps only `boards/`. Board apps become model + view (the
   scope rewritten that way). **PRISM's CI cross-compiles and tests them (decided):** the Arm
   toolchain from the development image (prism-stm32's `docker/Dockerfile`), FreeRTOS as a
   dependency, and the QEMU MPS2-AN500 (Cortex-M7) test firmwares moved from prism-stm32 run as
   a CI job, so a PRISM change cannot break the embedded side unnoticed.
   The manual page for `frame_app` and `prism::raster` comes with step 3, when the Framebuffer
   concept settles that API.
   Measured board baseline after step 1: 19-23 allocations per frame (same sites as before; the
   earlier "20" was already 21 before step 1).
2. **Sample streams:** the concepts, `Ring`, `Signal`, `any_source`; the STM32 ADC as a lending
   DMA source; the generator and WAV emulations. The scope reads through the family.
3. **Accelerated drawing:** the rasterizer templated on the pixel format, the optional
   operations, DMA2D on the STM32; measured against the CPU path with the timing line.
4. **Bus devices and lines** with the first driver that needs them (the MP3 player's SD card
   and audio codec), and the desktop emulation of the whole player.

Each step is measured on the board (fps, CPU, allocations) and must not regress it.

## Left to the builder

- Names and file layout inside `prism::platform` and `prism::dev`.
- Error type per family beyond the bus devices' `std::expected<void, std::error_code>`.
- Whether `any_source<T>` is hand-written or generated from the concept (hand-written first).
- How a Linux framebuffer/DRM target is built (it is a level-1 target; not in the first steps).
