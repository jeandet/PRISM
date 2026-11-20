# Profiling

**Status:** Proposal — for review before code.

## Why

PRISM now runs on a microcontroller, and the next optimisation choices there (redraw only what
changed, DMA2D, drawing into cacheable RAM: porting-and-devices.md) need to know where a frame's
time goes. Today there are only totals: the board's once-a-second line (update, snapshot, draw,
present per frame, CPU, heap) and, on the desktop, the snapshot's build time, `present_stats()`
and `examples/perf_lab`. Nothing shows the parts of a frame on a timeline. The emulator (Renode)
cannot answer it: it models no caches and no memory wait states.

The tool is for PRISM's own developers and for the people who write apps with PRISM: an app's
marks land on the same timeline as PRISM's, so a scope's FFT shows up next to PRISM's drawing.
It works the same on a microcontroller and on the desktop.

## Goals

- Marks for **blocks** (a duration, on a thread), **instants** (an event at a time),
  **counters** (a named value over time) and **frame boundaries**; public API in PRISM.
- **Post-mortem**, like gcov on a microcontroller: record into RAM while running; dump at the
  end (or on demand); a host tool turns the dump into a trace a viewer opens.
- **The device pushes the dump itself** through a byte stream (`io::ByteStream`): the UART behind
  the ST-LINK's virtual COM port on the board, a file on the desktop.
- **Free when off** (the default): marks compile to nothing, so they can stay in release code.
- **Cheap when on:** of the order of a hundred cycles per mark; no allocation and no lock once a
  thread has recorded its first mark (doc/design/allocation-free-frames.md).
- One viewer for both targets: **Perfetto** (ui.perfetto.dev, in a browser, nothing to install),
  through the Chrome trace-event JSON format, which Tracy can import too.

## Non-goals

- Live streaming and a live viewer. (A later transport can stream the same events.)
- Sampling profilers, call stacks, memory profiling beyond counters.
- A viewer of PRISM's own.
- Vendor tools (SEGGER SystemView, Percepio Tracealyzer): they do not cover the desktop and
  bring licences of their own.

## Anti-goals

- A mark that allocates, locks, formats a string or calls into the OS on the device.
- A build where profiling on and off produce different PRISM headers for a consumer (the flag
  travels on `prism_headers_dep`, the lesson of the porting work).

## Approaches considered

- **A. A recorder of PRISM's own, plus a converter to Chrome/Perfetto JSON (chosen).** Per-thread
  rings of fixed-size events in RAM; a dump in a small binary format; a host tool. Identical on
  both targets; free when off. Cost: we write the recorder and the converter.
- **B. Tracy everywhere.** Best desktop viewer, but no microcontroller client, and writing its
  capture format by hand means following an internal, versioned format.
- **C. A vendor tool** (SystemView, Tracealyzer). Polished, FreeRTOS-aware, post-mortem modes;
  no desktop, and licences.

## Design

### API (`include/prism/profile/profile.hpp`, namespace `prism::profile`)

```cpp
PRISM_PROFILE_SCOPE("layout");              // a block: from here to the end of the scope
PRISM_PROFILE_INSTANT("present");           // an instant event
PRISM_PROFILE_COUNTER("heap KB", value);    // a counter sample (integer)
PRISM_PROFILE_FRAME();                      // a frame boundary
prism::profile::name_thread("render");      // once per thread
prism::profile::dump(stream);               // io::ByteStream: write everything recorded so far
```

A mark records one pointer: each macro builds a `static constexpr` call site (name, function,
file, line, from `std::source_location`) and the event points at it; nothing is copied, hashed
or formatted. A name that is not a constant expression does not compile. The dump sends each
site's strings once, so the host needs no ELF. The macros exist
for one reason, compiling to nothing when profiling is off without evaluating their arguments;
each forwards to an inline function when on.

### Build switch

Meson option `profiling` (feature, default `disabled`). When enabled, `-DPRISM_PROFILING` goes on
`prism_headers_dep`, so PRISM and every consumer compile the marks the same way. A board build
enables it as any app would.

### Recorder

- **Per-thread ring** of fixed-size events: a timestamp (64-bit ticks), the name pointer, a kind
  (begin, end, instant, counter, frame) and a 32-bit value (the counter's). *Our call:* all
  rings live in one static region sized at build time (options `profiling_events` per thread and
  `profiling_threads`), not on the heap: no allocation at all, and a board can place the region
  (a named section, e.g. into SDRAM; it must be usable before the first mark). Defaults: 65536
  events × 32 threads on a hosted system; a bare-metal build sets its own (the STM32 board:
  1024 × 6, about 150 KB of its 1 MiB AXI SRAM).
- **Full ring: the oldest events are overwritten** (a flight recorder: the last N events per
  thread survive), and a per-thread counter records how many were lost. Our call: post-mortem
  wants the end of a run, the moment something went wrong.
- **Threads** are the OS's own: a FreeRTOS task (its handle is the id, so task-switch events
  land on the same track) or a desktop thread. *Our call:* `name_thread` copies the name into the
  slot (16 characters), so a task that ends leaves no dangling pointer; a finished thread keeps
  its slot and its events (post-mortem wants them); a thread beyond the table records nothing and
  is counted.
- **No marks in interrupt handlers (decided, v1).** An interrupt has no thread of its own and
  would write into the interrupted task's ring. Documented, and a debug check traps a mark in
  interrupt context (Cortex-M: IPSR ≠ 0). An interrupt ring of its own is later work.
- **Clock:** a port function `prism::platform::profile_ticks()` and `profile_ticks_per_second()`.
  Desktop and Linux: `std::chrono::steady_clock`. Cortex-M7: the DWT cycle counter, extended to
  64 bits. Requirement for the builder: monotonic per thread, at most 1 µs resolution, and the
  cost of a mark within budget. The extension is the builder's (a 32-bit cycle counter wraps
  every 15 s at 280 MHz).
- **FreeRTOS task switches** (optional, `ports/freertos`): the `traceTASK_SWITCHED_IN` hook writes
  an instant into a ring of its own, so the trace shows which task ran when.

### Dump

`prism::profile::dump(io::ByteStream auto& out)` writes a binary trace:

- header: magic, format version, ticks per second, pointer size, counts;
- the thread table: id, name, lost-event count;
- the names: each distinct name pointer seen, with its string (the device reads its own flash, so
  the host needs no ELF);
- each thread's events, oldest first.

*Our call:* recording pauses during a dump (marks become no-ops until it ends), so the end of
the run, which the flight-recorder choice exists for, is what gets sent; a slow UART dump takes
seconds. The dump is the only part that does I/O, and it runs on the thread that calls it.

*Our call:* the format is framed for a link without flow control: a sync marker, then chunks
with a length and a CRC-32, so the host tool finds a dump in a byte stream and rejects damaged
chunks instead of mis-reading them.

The board's transport is a UART `io::ByteStream` (prism-stm32: the USART wired to the ST-LINK's
virtual COM port, which the host sees as a serial port; *our call:* used only for dumps in v1,
at 921600 baud); the desktop's, a file stream. A small
fixed-size output buffer, so a dump allocates nothing either.

### Host tool `prism-trace` (PRISM, `tools/`)

`prism-trace <dump file | serial device> -o trace.json`: reads the binary trace (from a file, or
from a serial port with PRISM's `io::SerialPort`), writes Chrome trace-event JSON (`"ph":"B"/"E"`,
`"i"`, `"C"`, thread names as metadata). The binary format is one header shared by the recorder
and the tool, so they cannot drift.

### Marks PRISM places itself

`frame_app` (logic loop, update hooks, snapshot build, draw, present, frame boundary),
`WidgetTree` publish (layout, recording dirty widgets, flatten), the rasterizer (per draw-list,
and text), `model_app` on the desktop; counters for heap and CPU where the platform reports them.

### PC sampling (microcontroller, later)

A timer interrupt at the highest priority reads the interrupted code's PC from the exception
frame the CPU pushed and counts it in a histogram over `.text` (16-bit buckets of 64 bytes:
1/32 of the code size). The dump carries the histogram; `prism-trace` names the buckets from
the firmware's ELF. It shows where time goes that no mark covers, interrupt handlers and the
idle task included; no call stacks. On a desktop, `perf` does this better: not built there.

## Order

1. Recorder, API, build switch, desktop file dump, `prism-trace`, Perfetto check by hand.
2. PRISM's own marks; a desktop example (`perf_lab`) traced.
3. Cortex-M7 clock, QEMU test (record, dump over semihosting, convert), the board's UART stream,
   a traced scope run on the real board.
4. FreeRTOS task-switch events.
5. PC sampling on Cortex-M.

## Left to the builder

- The event layout in bytes and the binary format's exact fields (beyond the list above).
- The 64-bit extension of the cycle counter, within the stated requirement.
- The UART's baud rate and how a board app triggers a dump (a button, after N frames, on quit).
- Naming of files under `include/prism/profile/` and `tools/`.
