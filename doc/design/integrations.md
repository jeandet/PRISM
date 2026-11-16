# Integrations: Audio, Networking, Serial

Design note for plan item 19 of `doc/review-2026-11-13.md`. Date: 2026-11-15.

## Overview

PRISM does not implement audio. It does not implement sockets, serial ports or MQTT.
It ships thin adapters over established libraries, and the seams those adapters use are public.
Any app can plug in a different library through the same seams.

Two rules follow from that.

1. A domain library is wrapped, never rewritten. Qt's `QAudio`/`QNetwork` route, and the route the
   user asked for: "I didn't say we should write but just wrap modern well maintained and
   established C++ libs".
2. An adapter is not the seam. The seam is a concept plus a customization point, shipped so the
   common case needs no code — and so another library can take its place.

## What exists today

Facts, from this tree.

- No audio, networking, serial or MQTT library is linked. `subprojects/*.wrap` holds SDL3 and its
  satellite libs, FreeType, HarfBuzz, libunibreak, sheenbidi, Glaze, fmt, doctest, nanobind,
  robin-map, sqlite3, stdexec, magic_enum and wgpu-native. Nothing else.
- `prism_core_dep` links stdexec, fmt, magic_enum, libunibreak, sheenbidi and Glaze. No SDL.
- `audio::Output` (`include/prism/audio/output.hpp`, SDL in `src/backends/sdl_audio.cpp`) is
  pilot 1, done 2026-11-16: a playback device calling a render function, with a timer fallback
  when no device opens. The synth rack plays through it; it was the example's own SDL wrapper.
- `include/prism/core/latest.hpp` holds `core::Latest<T>` (it was the synth rack's `RtLatest<T>`), a three-slot handoff with no lock
  and no allocation, because `Shared<T>::set()` allocates a `shared_ptr` and cannot run in a
  callback.
- `examples/model_lab_bench/` already crosses threads with no adapter: `Shared<DaqSettings>` into a
  worker, `Shared<DaqTelemetry>` and `Channel<DaqEvent>` back out. Its link is `hardware_read()`, a
  fake that sleeps.

The crossing mechanism works. What is missing is the shape around it: the audio callback has a
hand-rolled library wrapper, and the bench link has no interface at all.

## The seams

### 1. Crossing into the model

Four ways, and the choice is not taste — it follows from the producer.

| Seam | Semantics | Allocates | Legal producer | Use for |
|---|---|---|---|---|
| `Field<T>` | one value, logic thread | on change | logic thread only (`set()` asserts `may_change_model()`) | model state the UI edits |
| `Shared<T>` | latest value, coalescing | yes, one `shared_ptr` per `set()` | any thread | telemetry, settings pushed to a worker |
| `Channel<T>` | ordered, lossless, unbounded queue | yes (`simplify:` in `channel.hpp`) | any thread | events that must not be lost |
| `Latest<T>` | latest value, no lock, no alloc | no | one RT producer → anyone | audio callback → UI |

The fourth landed with plan item 18 (it was the synth rack's `RtLatest`):

```cpp
// include/prism/core/latest.hpp
template <typename T>  // trivially copyable; one producer, one consumer
class Latest {
public:
    void publish(const T& value) noexcept;   // writer side, never blocks, never allocates
    T read() noexcept;                       // reader side: the newest value, whole
};
```

On the logic thread side, `ctx.receive(shared_or_channel, callback)` is the one call that hands
what another thread delivered to the model (it binds the waker and drains on wake).

The rule that keeps this honest: a chain of updates always ends in exactly one of these four, and
the adapter never exposes a raw callback that writes a `Field`. A network thread cannot touch the
widget tree.

### 2. Concepts for the recurring shapes

Three shapes cover audio, sockets, serial and MQTT. Keep each concept to the minimum an adapter
needs, not to what a given library happens to offer.

```cpp
namespace prism::io {
template <typename S>
concept ByteStream = requires(S s, std::span<const std::byte> out, std::span<std::byte> in) {
    { s.write(out) } -> std::convertible_to<std::size_t>;
    { s.read(in) } -> std::convertible_to<std::size_t>;
    { s.close() } -> std::same_as<void>;
};

template <typename Q, typename Request>
concept Requester = requires(Q q, Request req) {
    { q.send(req) } -> std::same_as<void>;   // async; the reply arrives elsewhere
    { q.on_reply([](const Request&, std::span<const std::byte>) {}) };
};
}  // namespace prism::io
```

Audio is not a stream. It is a pull: the device asks for N frames by a deadline, and missing the
deadline is an audible click.

```cpp
template <typename S>
concept Sink = requires(S s, std::span<float> frames) {
    { s.sample_rate() } -> std::convertible_to<int>;
    { s.render(frames) } -> std::same_as<void>;   // fills interleaved float
};
```

`ByteStream` covers TCP, UDP datagrams (one packet per call), serial, a pipe, an in-memory buffer
and a test double. That is the point: the lab bench's fake link and a real instrument are the same
type.

### 3. ADL customization points

`Widget<T>` adapts a value type without inheritance: it is selected at compile time by concept.
Third-party handles get the same treatment in the I/O layer, with a free function found by ADL.

```cpp
namespace prism::io::adl {
struct no_customization {};
// Poison pill: with no user overload, this one wins and the CPO is ill-formed.
template <typename S>
no_customization write(S&, std::span<const std::byte>);
}  // namespace prism::io::adl

namespace prism::io {
inline constexpr struct write_fn {
    template <typename S> requires ByteStream<S>
    std::size_t operator()(S& s, std::span<const std::byte> bytes) const {
        return write(s, bytes);  // unqualified: the user's overload wins
    }
} write{};
}  // namespace prism::io
```

An app adapts `asio::ip::tcp::socket` by writing that overload next to it. Adding a free function to
another library's namespace is legal for non-`std` namespaces, but it can collide with a future
version of that library. When that risk is unacceptable, the app specializes a trait in its own
namespace instead, and the default CPO forwards to it:

```cpp
template <> struct prism::io::adapter<asio::ip::tcp::socket> { /* write, read, close */ };
```

Two mechanisms, one rule: no adapter requires modifying the third-party type, and no adapter
requires modifying PRISM.

### 4. Senders for "do this, then set that"

stdexec is already a dependency and already carries `run_loop`, `schedule_tick` and
`AppContext::post`. A request/response exchange is a sender chain.

```cpp
auto fetch = stdexec::schedule(pool.get_scheduler())
           | stdexec::then([req] { return http_get(req); })
           | stdexec::then([&](Response r) { telemetry_ingest.set(parse(r)); });
stdexec::start_detached(std::move(fetch));
```

The last step is always a crossing from the table above. `telemetry_ingest` is `Shared<T>` because
the pool thread is not the logic thread. Writing `model.pressure.set(...)` there would trip the
`may_change_model()` assertion. That is the design working, not an inconvenience.

## Candidates

Checked on each library's official site or repository, on 2026-11-15. "unchecked" means the project
does not state it and this note did not verify it.

| Domain | Candidate | Licence | Maintenance | C++ | wasm | ESP32 | Meson/WrapDB |
|---|---|---|---|---|---|---|---|
| Audio | SDL3 audio | zlib | active, SDL3 3.4.0 already wrapped here | C99 API, C++ usable | yes, Emscripten | no ESP32 target | already in `subprojects/` |
| Audio | miniaudio | public domain or MIT-0 | single-file, active | C89, C++ usable | yes, WebAudio | no backend, custom one needed | on WrapDB (0.11.22-2) |
| Sockets, serial | Asio (standalone) | BSL-1.0 | 1.38.2 stable, active | C++11 and later | no, POSIX and Windows only | sockets only, via `espressif/asio` | on WrapDB (1.38.2-1) |
| HTTP | cpp-httplib | MIT | ~17k stars, active | C++11 | no, blocking sockets | **no, 32-bit unsupported** | on WrapDB (0.66.0-1) |
| HTTP, WebSocket | Boost.Beast | BSL-1.0 | active, part of Boost | C++11 | unchecked, no Emscripten note | unchecked, Boost ports exist | requires Boost |
| MQTT | Eclipse Paho MQTT C++ | EPL-2.0 or EDL-1.0 | active | C++17 | no, memory-managed OSes only | no IDF component | CMake only, no Meson |
| MQTT | async_mqtt | BSL-1.0 | 10.3.0, active | C++17 | unchecked, no wasm note | unchecked, no ESP32 note | requires Boost ≥ 1.84 |

Where the facts come from, checked 2026-11-15:

- miniaudio: https://miniaud.io/ — single source file, no external dependencies, "public domain" or
  MIT No Attribution, platforms listed are Windows, macOS/iOS, Linux, BSD, Android and Web.
- Asio: https://think-async.com/Asio/ and
  https://think-async.com/Asio/asio-1.38.2/doc/asio/overview.html — Boost Software License,
  standalone C++11, overview lists networking, timers, files, pipes and **serial ports**.
- cpp-httplib: https://github.com/yhirose/cpp-httplib — MIT, header-only client and server,
  HTTP/1.1 only, explicit warning that 32-bit platforms are not supported.
- Boost.Beast: https://github.com/boostorg/beast — "HTTP and WebSocket built on Boost.Asio in
  C++11", BSL-1.0.
- Paho MQTT C++: https://github.com/eclipse-paho/paho.mqtt.cpp — C++17, CMake only, EPL-2.0
  dual-licensed with EDL-1.0, MQTT 3.1/3.1.1/5, TCP/TLS/WebSocket.
- async_mqtt: https://github.com/redboltz/async_mqtt — C++17, MQTT 3.1.1 and 5.0, client and broker,
  requires Boost ≥ 1.84.
- SDL3 platforms: https://github.com/libsdl-org/SDL/blob/main/docs/README-platforms.md — consoles,
  mobile, desktop and Emscripten; no microcontroller target.

Checked later the same day, for the cells that first read "unchecked":

- miniaudio platforms: https://github.com/mackron/miniaudio#supported-platforms — Windows, macOS/iOS,
  Linux, the BSDs, Android, Raspberry Pi, Emscripten, plus a null backend. Other platforms compile
  "but will not include any support for playback or capture by default" without a custom backend.
  So there is no ESP32 backend.
- Asio licence and tested platforms: https://think-async.com/Asio/ — Boost Software License;
  tested on Linux, macOS, Windows and FreeBSD. No WebAssembly target is listed.
- Asio on ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/protocols/asio.html
  and https://docs.espressif.com/projects/esp-protocols/asio/docs/latest/index.html — the
  `espressif/asio` component, version 1.32.0 on the Component Registry. Its supported-features
  section says it "currently supports only network asynchronous socket operations; does not
  support serial port".
- WrapDB index: https://github.com/mesonbuild/wrapdb/blob/master/releases.json — `asio` 1.38.2-1,
  `cpp-httplib` 0.66.0-1 and `miniaudio` 0.11.22-2 are entries. No Boost, Beast, Paho or
  async_mqtt entry.
- async_mqtt licence: https://github.com/redboltz/async_mqtt/blob/main/LICENSE — Boost Software
  License 1.0.
- cpp-httplib on the web: https://github.com/yhirose/cpp-httplib — its own README says the library
  "uses 'blocking' socket I/O", and names no Emscripten or WebAssembly target.
- Paho MQTT C++ scope: https://github.com/eclipse-paho/paho.mqtt.cpp — README: "for memory-managed
  operating systems such as Linux, MacOS, and Windows". The ESP Component Registry
  (https://components.espressif.com/) has no Paho entry; `espressif/esp-mqtt` is the IDF client.
- Boost.Beast on the web and on ESP32: https://github.com/boostorg/beast — no Emscripten or ESP32
  statement in its README, so still unchecked. Boost itself is packaged for ESP32 as
  `espressif/esp-boost`; whether Beast is in it was not checked.
- async_mqtt on the web and on ESP32: https://github.com/redboltz/async_mqtt — README states C++17
  and Boost ≥ 1.84, and names no Emscripten or ESP32 target, so still unchecked.

### Recommended, per domain

**Audio: SDL3, with miniaudio as the second adapter.** SDL3 is already linked, already exercised by
the synth rack, and covers every platform PRISM builds for today, including the browser. Adding
miniaudio as the default would put a second audio stack in a build that already has one. miniaudio
earns its place as the adapter for builds without SDL — it is one header, one file, and no build
system change. It is not the board's audio path: miniaudio ships no ESP32 backend, so on the P4 the
ESP-IDF I2S driver would sit behind the same `Sink` concept.

**Sockets and serial: Asio standalone.** One library covers TCP, UDP and serial ports with the same
`async_read`/`async_write` shape, which means one `ByteStream` adapter instead of three. Standalone
keeps Boost out of the Meson build, and WrapDB already carries `asio` 1.38.2-1, so the dependency
costs one wrap file. Writing a serial port by hand means `termios` and a platform-specific mess —
exactly the reimplementation this note rules out. On ESP32 the `espressif/asio` component gives the
socket half for free and the serial half not at all, so `serial_link()` stays a host adapter and the
board's UART goes through the ESP-IDF driver.

**HTTP: the user wants an API in the style of Python's `requests` or `httpx`** (2026-11-16): one
call per request, a response object with status, headers, text and json, and an async form whose
reply crosses into the model like any other. Candidates named: cpp-httplib (below) and aeronet
(https://github.com/sjanel/aeronet, not yet checked). Under it, the byte stream is PRISM's
`QIODevice`: one `ByteStream` shape for TCP, serial, files, pipes, a buffer and a test double.

**HTTP, first assessment: no default; cpp-httplib when an app asks for it, Beast when WebSocket matters.** HTTP is a
protocol on top of a socket, not a separate I/O domain, so it does not get its own dependency by
default. cpp-httplib is the cheapest way to make one request from app code and runs on its own
thread, which the `Shared`/`Channel` crossing already supports. Its README states that 32-bit
platforms are unsupported, so it is not a candidate on the S3.

**MQTT: Eclipse Paho MQTT C++.** Its documented threading model — one internal network thread,
callbacks must not block — matches PRISM's crossing rule, so the adapter's job is a `Channel` write
and nothing else. async_mqtt is the better choice only when a broker or MQTT 5 topic aliases are
needed, and it pulls in Boost. On the board neither applies: Paho scopes itself to memory-managed
OSes and has no Component Registry entry, so the P4's MQTT path is `espressif/esp-mqtt` behind the
same adapter.

## Pilot 1: audio out of the synth rack

**Done 2026-11-16**, simpler than planned below: no `Sink` concept yet, one concrete
`audio::Output(Format, RenderFn)` whose SDL code lives in the backend library. The render function
gets a `std::span<float>` of interleaved samples. `tests/test_audio_output.cpp` runs it on SDL's
dummy audio driver (added to `sdl3-headless-drivers.patch`) and on the timer, and checks that the
rendering thread allocates nothing between blocks. → skipped: a concept and a second backend
(miniaudio, ESP-IDF I2S); add when a build without SDL needs sound.

The plan as first written:

Replaces `examples/model_synth_rack/audio_device.hpp`. Same behaviour, but SDL stops leaking into
the example and the callback contract becomes public.

Files:

- `include/prism/audio/sink.hpp` — the `Sink` concept, `SinkConfig`, and `prism::audio::null_sink()`
  for tests and headless runs.
- `include/prism/audio/sdl_sink.hpp`, `src/audio/sdl_sink.cpp` — moves the existing SDL audio code
  down to the backend layer, beside the other SDL code.
- `examples/model_synth_rack/src/audio_device.hpp` — deleted; the example keeps only the parts that
  are about the synth (callback load measurement stays, it is app-level instrumentation).

What an app writes:

```cpp
auto sink = prism::audio::open_sink({.sample_rate = 48000, .channels = 2, .frames = 512});
sink->render([&](std::span<float> out) noexcept { rack.render(out, state.read()); });
```

Tests that prove it:

- A counting `operator new` around N blocks of `null_sink` rendering: zero allocations inside the
  callback. This is the constraint that `Shared<T>` fails today, stated as a test.
- Block count and frame count per block match the config, on the timer fallback path too.
- `Latest<SynthState>` publish/read round trip: the reader sees the newest published value, never a
  torn one, under a writer thread on a different core.

The existing example keeps working unchanged from the user's side. `pump()` and the timer fallback
move into the adapter, so the example drops roughly 150 lines and one SDL include.

## Pilot 2: the lab bench over a real link

Chosen over MQTT because the example's fake link is exactly the shape of a blocking
request/response exchange, and a real instrument over TCP is the shortest path from the demo to
something a user can point at hardware.

Files:

- `include/prism/io/byte_stream.hpp` — the concept above, no dependency.
- `include/prism/io/asio_link.hpp`, `src/io/asio_link.cpp` — `prism::io::tcp_link()` and
  `prism::io::serial_link()`, both `ByteStream`, both writing through the ADL CPO.
- `examples/model_lab_bench/src/scpi_link.hpp` — line framing and the SCPI command/reply pair,
  which is the `Requester` shape specialised to text.
- `examples/model_lab_bench/model_lab_bench.cpp` — `hardware_read()` gains a link-backed path,
  selected by `--instrument host:port`. The simulator stays the default.

What an app writes:

```cpp
auto link = prism::io::tcp_link({.host = "192.168.1.42", .port = 5025});
link->on_line([&](std::string_view line) { telemetry_ingest.set(parse_measurement(line)); });
link->send("*MEAS:VOLT?\n");
```

Tests that prove it:

- Loopback test: an in-process acceptor speaking canned SCPI, asserting the resulting
  `DaqTelemetry` equals what `synthesize_telemetry()` produces for the same settings. Same numbers,
  different source, so the model needs no change.
- The existing stall test must still pass. A link that never replies must not freeze the UI, nor
  grow memory without bound. That second half is new: today the queue is unbounded.
- `ByteStream` conformance for the test double and the TCP link, as one template test over both.

## What stays out

- No TLS policy, no certificate store, no DNS resolver of PRISM's own. Asio and Paho bring those.
- No audio DSP, no codecs, no resampling, no MIDI. miniaudio's decoders and node graph stay unused.
- No Bluetooth, no USB stack, no serial port enumeration beyond what the OS reports.
- No MQTT broker, even though async_mqtt can be one.
- No browser networking beyond what Emscripten provides. Sockets do not exist there.

## Open questions

1. Where do the adapters live: `include/prism/{audio,io}`, or a separate repository? New modules do
   not violate the SDL-free rule for core, but they do grow the build.
2. Meson optionality. `dependency('asio', required: false)` plus a `feature` option per domain keeps
   the default build unchanged. Does a missing Asio fail loudly, or fall back to the test double?
3. Does `Latest<T>` go in core (plan item 18 says yes) or beside the audio adapter?
4. Backpressure on `Channel<T>`. A fast link and a slow UI grow an unbounded queue. Today's answer:
   coalesce into `Shared<T>` when only the latest value matters, `Channel<T>` for events. Whether
   that is enough needs a measurement, not an opinion.
5. Asio on an ESP32 over lwIP and FreeRTOS: sockets only. The `espressif/asio` component — 1.32.0 on
   the Component Registry, the port ESP-IDF moved out of tree at v5.0, now living in
   `espressif/esp-protocols` — states that it "currently supports only network asynchronous socket
   operations; does not support serial port", with SSL/TLS off by default. That is enough for a TCP
   or WebSocket transport and not enough for `serial_link()`, so on the board the UART path has to
   be an ESP-IDF driver adapter rather than reuse of the host one. The adapter interfaces still
   match, so the split costs an implementation, not a design change.
