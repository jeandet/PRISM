# Profiling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Post-mortem profiling for PRISM and apps on the desktop and on a Cortex-M7: free-when-off
marks, per-thread rings, a framed binary dump over any `io::ByteStream`, and `prism-trace` turning
it into Perfetto JSON.

**Architecture:** A `Recorder` class over caller-provided storage (testable without the build
option); one global recorder over a static region exists only with `-Dprofiling=enabled`, which
also puts `PRISM_PROFILING` on `prism_headers_dep` so the `PRISM_PROFILE_*` macros expand the same
everywhere. A shared header fixes the binary format; the dump and `prism-trace` both use it.

**Tech Stack:** C++26, meson, doctest, FreeRTOS (ports/freertos), QEMU mps2-an500, Perfetto /
Chrome trace-event JSON.

**Spec:** `doc/design/profiling.md`

**Execution (user's choice):** implementer = Mistral vibe, headless and edit-only (no shell, no
builds, no git), two steps per task: (1) tests + stubs ("TESTS-READY"), controller builds red on
Coder; (2) implementation ("IMPL-READY"), controller builds green, commits; a Claude reviewer
checks each task.

## Global Constraints

- PRISM style: AGENTS.md (headers keep templates, `src/` the rest; no bare `detail` namespace;
  `simplify:` on deliberate cuts; no mutable `inline` variable in a header; a `thread_local` that
  must be shared lives behind a function in `src/core/shared_state.cpp` — a Windows DLL cannot
  export one).
- A mark: no allocation, no lock, no formatting, no OS call once its thread has a ring; of the
  order of 100 cycles. The global region is static: no heap use at all.
- Off by default: `profiling` is a feature option, default `disabled`; when off, `PRISM_PROFILE_*`
  expand to nothing and do not evaluate their arguments.
- `PRISM_PROFILING`, `PRISM_PROFILING_EVENTS`, `PRISM_PROFILING_THREADS` travel on
  `prism_headers_dep` (consumers compile the macros the same way as libprism).
- Defaults: 65536 events × 32 threads on a hosted system; options `profiling_events`,
  `profiling_threads` (integers) override; a bare-metal board sets its own (the STM32: 1024 × 6).
- Thread name: copied, at most 16 characters. Full ring: oldest events overwritten, counted per
  thread. Table full: the thread records nothing, counted. Recording pauses during a dump.
- No marks in interrupt handlers (v1): a debug check traps one on Cortex-M (IPSR ≠ 0).
- Binary format: little-endian; sync marker, then chunks of (type, length, payload, CRC-32 IEEE);
  version 1; one header shared by recorder, dump and tool.
- Builds only on Coder, one at a time (`/tmp/claude-6516/prism_remote.sh`; prism-stm32
  `scripts/remote.sh`); build dirs `builddir*`; nothing pushed without the user.

## Review Focus

- Two threads marking while a third dumps: the dump is consistent (no torn event) and marks
  resume after it. → Task 2 test.
- After a ring wraps, an `end` whose `begin` was overwritten, and a `begin` never ended: the JSON
  is still valid (orphan ends dropped, open begins closed at the thread's last timestamp). → Task 3.
- A damaged chunk (bad CRC) or garbage before the sync marker: the tool skips it, reports it,
  converts the rest. → Task 3 test.
- A 32-bit target (pointer size 4) produces a dump the 64-bit host reads correctly. → Task 5 test.
- More threads than the table: no crash, the extra threads are counted in the dump. → Task 1 test.

---

### Task 1: Recorder, API, build switch

**Files:**
- Create: `include/prism/profile/profile.hpp` (macros + `name_thread`), `include/prism/profile/recorder.hpp`, `src/profile/recorder.cpp`, `src/profile/global.cpp` (only with profiling enabled), `src/profile/steady_clock.cpp` (hosted only)
- Modify: `meson_options.txt`, `meson.build`, `src/meson.build`, `src/core/shared_state.cpp` (if a thread_local is needed), `tests/meson.build`
- Test: `tests/test_profile_recorder.cpp`, `tests/test_profile_macros_off.cpp`

**Interfaces (namespace `prism::profile`):**
- `enum class Kind : std::uint8_t { begin, end, instant, counter, frame };`
- `struct Event { std::uint64_t ticks; const char* name; std::int32_t value; Kind kind; };`
- `class Recorder { public: Recorder(std::span<Event> storage, std::size_t threads); void begin(const char* name); void end(const char* name); void instant(const char* name); void counter(const char* name, std::int32_t v); void frame(); void name_thread(std::string_view name); void pause(); void resume(); struct ThreadView { std::string_view name; std::uint64_t id; std::uint64_t lost; std::span<const Event> first, second; }; template <class F> void for_each_thread(F&&) const; std::uint64_t threads_dropped() const; };` — storage split evenly between `threads` rings; a thread gets its ring at its first mark; `for_each_thread` gives the ring oldest-first as up to two spans.
- `std::uint64_t prism::platform::profile_ticks(); std::uint64_t prism::platform::profile_ticks_per_second();` — defined by `steady_clock.cpp` on hosted systems, by the port on bare metal (Task 5).
- `Recorder& global();` — only with `PRISM_PROFILING`; static storage of `PRISM_PROFILING_EVENTS * PRISM_PROFILING_THREADS` events.
- Macros: `PRISM_PROFILE_SCOPE(name)` (RAII begin/end), `PRISM_PROFILE_INSTANT(name)`, `PRISM_PROFILE_COUNTER(name, value)`, `PRISM_PROFILE_FRAME()`; `prism::profile::name_thread(std::string_view)` (no-op when off).

- [ ] **Step 1 (vibe): tests + stubs.** `test_profile_recorder.cpp`, each over a local `std::vector<Event>(64)` and `Recorder r{storage, 4}`:
  - "a block records begin and end with increasing ticks on its thread": `r.begin("a"); r.end("a");` → one thread, events `{begin,"a"}`, `{end,"a"}`, `t0 <= t1`.
  - "a full ring keeps the newest events and counts the lost ones": 16 events per ring (64/4), record 20 instants → ring holds the last 16 (names in order), `lost == 4`.
  - "each thread gets its own ring": two `std::jthread`s each record 3 instants → two ThreadViews with 3 events each.
  - "a thread beyond the table is counted, not recorded": `Recorder r{storage, 1}`, a second thread marks → `threads_dropped() == 1`, no crash.
  - "a paused recorder drops marks": `pause(); instant("x"); resume(); instant("y");` → only "y".
  - "a thread name is copied and cut at 16 characters".
  `test_profile_macros_off.cpp` (built without `PRISM_PROFILING`): `int n = 0; PRISM_PROFILE_COUNTER("c", ++n); PRISM_PROFILE_SCOPE("s"); CHECK(n == 0);`
  Stubs so it compiles. Controller: build — Expected: tests fail (stubs), macros test passes only once the macros exist.
- [ ] **Step 2 (vibe): implement.** Ring index per thread: a slot table filled with an atomic counter on first mark; the calling thread finds its ring through a `thread_local` cache (keyed by the recorder, so tests with several recorders work). Options `profiling` (feature, disabled), `profiling_events` (int, 65536), `profiling_threads` (int, 32); the three defines on `prism_headers_dep` when enabled.
- [ ] **Step 3 (controller):** `meson test -C builddir` all pass; `builddir-prof` (`-Dprofiling=enabled`) builds and passes; `builddir-nosdl`, `builddir-arm` build; header syntax checks (no-reflection, clang+libc++).
- [ ] **Step 4: Commit** `feat(profile): recorder, PRISM_PROFILE_* marks, -Dprofiling`.

### Task 2: Binary format and dump

**Files:**
- Create: `include/prism/profile/trace_format.hpp` (constants, chunk types, `crc32`), `include/prism/profile/dump.hpp`, `include/prism/io/file_stream.hpp` (+ `src/io/file_stream.cpp` if not header-only), `include/prism/io/memory_stream.hpp`
- Test: `tests/test_profile_dump.cpp`

**Interfaces:**
- `trace_format.hpp`: `inline constexpr std::array<std::byte,4> sync_marker{'P','R','T','R'}; inline constexpr std::uint16_t format_version = 1; enum class Chunk : std::uint8_t { header, thread, names, events, end };` and `constexpr std::uint32_t crc32(std::span<const std::byte>)` (IEEE, reflected, table at compile time). Chunk = type (u8), payload length (u32 LE), payload, CRC-32 of type+length+payload (u32 LE). Header payload: version, pointer size, ticks per second, thread count, threads dropped. Thread: id (u64), lost (u64), name (len u8 + bytes). Names: (pointer u64, len u16, bytes) per distinct name. Events: thread id then (ticks u64, name pointer u64, value i32, kind u8) each, oldest first, at most 4096 per chunk.
- `template <io::ByteStream S> std::expected<void, std::error_code> dump(const Recorder&, S& out);` — pauses the recorder for the duration (resumes on every exit path); uses a fixed 512-byte output buffer.
- `io::MemoryStream` (a `std::vector<std::byte>` ByteStream), `io::FileStream::open(path, mode)` (ByteStream over a file).
- `std::expected<void, std::error_code> prism::profile::dump_to_file(std::string_view path);` (only with `PRISM_PROFILING`; dumps `global()`).

- [ ] **Step 1 (vibe): tests + stubs.** `crc32` of "123456789" is `0xCBF43926`; a recorder with two threads and three names dumped into a MemoryStream starts with the sync marker, has header/thread/names/events/end chunks in that order, each CRC valid; "marks during a dump are dropped and resume after" (a thread marking in a loop while `dump` runs: the dump's events are a prefix-consistent copy, every event in it well-formed, and the thread's ring has new events after); FileStream writes and reads back.
- [ ] **Step 2 (vibe): implement.**
- [ ] **Step 3 (controller):** suites as in Task 1. **Step 4: Commit** `feat(profile): framed binary dump over any byte stream`.

### Task 3: `prism-trace`

**Files:**
- Create: `tools/prism-trace/trace_reader.{hpp,cpp}`, `tools/prism-trace/chrome_json.{hpp,cpp}`, `tools/prism-trace/main.cpp`, `tools/prism-trace/meson.build`
- Modify: `meson.build` (`subdir('tools/prism-trace')` on hosted systems)
- Test: `tests/test_prism_trace.cpp`

**Interfaces:**
- `struct Trace { double ticks_per_second; struct Thread { std::uint64_t id; std::string name; std::uint64_t lost; std::vector<Event> events; }; std::vector<Thread> threads; std::map<std::uint64_t, std::string> names; std::uint64_t threads_dropped; std::vector<std::string> warnings; };`
- `Trace read_trace(std::span<const std::byte>);` — finds the sync marker (skips anything before it), skips chunks with a bad CRC (a warning each), works for pointer size 4 and 8.
- `std::string to_chrome_json(const Trace&);` — `{"traceEvents":[...]}`: `"ph":"B"`/`"E"` (ts in µs, pid 1, tid = thread id), `"i"` with `"s":"t"`, `"C"` with `"args":{"value":v}`, frames as instant `"frame"`, `"M"` thread_name metadata; orphan `E` dropped, open `B` closed at the thread's last timestamp.
- CLI: `prism-trace <file|serial-device> [-o out.json] [--baud N]`; a path under `/dev/` or `COM` reads through `io::SerialPort` (only when built with `io`), else a file; prints warnings and a one-line summary.

- [ ] **Step 1 (vibe): tests + stubs.** Round trip: record (two threads, a block, an instant, a counter, a frame) → `dump` → `read_trace` → `to_chrome_json` parses as JSON (doctest-checked with a small JSON check, or Glaze, already a dependency) and has the expected `ph` values and thread names; a byte flipped in the events chunk → that chunk skipped with one warning, other chunks still read; 10 garbage bytes before the sync marker → read fine; a ring that wrapped mid-block → valid JSON per Review Focus.
- [ ] **Step 2 (vibe): implement.**
- [ ] **Step 3 (controller):** suites; run `prism-trace` on a dump from the Task 4 example; the JSON loads with `python3 -m json.tool`. **Step 4: Commit** `feat(tools): prism-trace, dumps to Perfetto JSON`.

### Task 4: PRISM's own marks; a traced desktop run

**Files:**
- Modify: `src/platform/frame_runtime.cpp` (logic loop, hooks, snapshot, draw, present, frame, counters CPU/heap when `system_counters()` has them), `src/app/widget_tree.cpp` (publish: layout, refresh dirty, flatten), `src/raster/draw_snapshot.cpp` (per draw list; text), `include/prism/app/model_app.hpp` or `src/app/model_app.cpp` (logic loop, publish, frame), `examples/perf_lab/main.cpp` (`--trace FILE`: dump on exit)
- Test: `tests/test_profile_marks.cpp` (registered only when profiling is enabled)

**Interfaces:** consumes Tasks 1-2. Mark names (string literals, fixed): `"logic loop"`, `"update hooks"`, `"snapshot"`, `"layout"`, `"record dirty"`, `"flatten"`, `"draw"`, `"draw list"`, `"text"`, `"present"`; threads named `"logic"`, `"render"`.

- [ ] **Step 1 (vibe): test + stubs.** With `-Dprofiling=enabled`: a `frame_app` run (memory sink, as `tests/test_frame_app.cpp`) quitting after 3 presents → `global()` has a thread named "logic" with ≥ 3 "snapshot" blocks and a "render" thread with "draw" and "present" blocks, properly nested.
- [ ] **Step 2 (vibe): implement** the marks and perf_lab's `--trace`.
- [ ] **Step 3 (controller):** suites in builddir and builddir-prof; `perf_lab --headless 3 --trace /tmp/x.prtr` in builddir-prof, `prism-trace` on it, JSON valid; the user opens it in ui.perfetto.dev. **Step 4: Commit** `feat(profile): PRISM's frame, publish and raster marks`.

### Task 5: Cortex-M7 clock and the QEMU test

**Files:**
- Create: `ports/freertos/profile_clock.c` (`prism_profile_ticks`, `prism_profile_ticks_per_second` + C++ wrappers in `ports/freertos/system_counters.cpp` or a sibling), `tests/embedded/profile_qemu/main.cpp`
- Modify: `ports/freertos/meson.build`, `tests/embedded/meson.build`, `src/profile/recorder.cpp` (IPSR check under `__ARM_ARCH_7EM__`), `ports/freertos/meson.build` comment (port requirements: DWT, the clock)

**Interfaces:** the port defines Task 1's `profile_ticks()` / `profile_ticks_per_second()`: the DWT cycle counter (enable TRCENA in DEMCR, CYCCNTENA in DWT_CTRL), extended to 64 bits; if the counter does not advance (QEMU does not model it), fall back to `prism_now_us()` (µs, from posix_time.c) and report 1 000 000 ticks per second.

- [ ] **Step 1 (vibe): test + stubs.** QEMU firmware `profile_qemu`: a `Recorder` over a static 1024-event region, two tasks marking blocks; ticks strictly increase per thread; dump into a MemoryStream; `read_trace` (compile tools/prism-trace/trace_reader.cpp into the firmware) returns 2 threads, pointer size 4, the names; prints "failures: 0". A mark from an interrupt (trigger SysTick-context call via a FreeRTOS tick hook test) is out of scope for the test: the IPSR check is reviewed by reading.
- [ ] **Step 2 (vibe): implement.**
- [ ] **Step 3 (controller):** Coder cross build; ELF copied back; local `qemu-system-arm` run passes. **Step 4: Commit** `feat(ports): Cortex-M7 profile clock; profile round trip on QEMU`.

### Task 6: The board dumps over the ST-LINK's serial port (prism-stm32)

**Files (prism-stm32):**
- Create: `boards/stm32h7b3i_dk/uart_stream.{hpp,cpp}` (an `io::ByteStream` over USART1, PA9/PA10, the ST-LINK virtual COM port — check the board's schematic/BSP for the exact USART and pins — 921600 baud, blocking writes, no DMA in v1)
- Modify: `meson.build` (`profiling=enabled`, `profiling_events=1024`, `profiling_threads=6` in the PRISM subproject's options; HAL UART module), `boards/stm32h7b3i_dk/stm32h7xx_hal_conf.h`, `boards/stm32h7b3i_dk/main.cpp` (dump on the blue user button: `BSP_PB_Init` + poll in an `on_loop` hook), `README.md` (how to capture: `prism-trace /dev/tty.usbmodem… -o trace.json`, open ui.perfetto.dev)

- [ ] **Step 1 (vibe):** implement (no host test: hardware). **Step 2 (controller):** firmware builds, flash ≤ 1170 KB + the profiling region in RAM fits (`size` report); Renode run still shows the scope. **Step 3 (user):** flash, press the button, capture, open in Perfetto. **Step 4: Commit** `feat(board): profiling on, dump over the ST-LINK serial port`.

### Task 7: FreeRTOS task switches

**Files:** `ports/freertos/profile_task_switch.c` (the hook), `ports/qemu_mps2/FreeRTOSConfig.h` and prism-stm32's `boards/stm32h7b3i_dk/FreeRTOSConfig.h` (`#define traceTASK_SWITCHED_IN() prism_profile_task_switched_in()` under `PRISM_PROFILING`), `include/prism/profile/recorder.hpp` (`void task_switched_in(std::uint64_t task_id)` writing into a ring of its own, interrupt-safe: called from PendSV), `tools/prism-trace/chrome_json.cpp` (a "running" `B`/`E` slice per task between its switch-in and the next switch), test in `tests/embedded/profile_qemu`.

- [ ] **Step 1 (vibe): test + stubs:** two tasks alternating; the dump's switch ring names both task ids; the JSON has "running" slices on both tracks. **Step 2 (vibe): implement.** **Step 3 (controller):** QEMU run; board build. **Step 4: Commit** in each repo.
