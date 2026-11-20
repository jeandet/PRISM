// The binary dump (include/prism/profile/dump.hpp + trace_format.hpp over any io::ByteStream,
// doc/design/profiling.md): crc32's known answer; the framed chunk layout of a two-thread,
// three-name dump (sync marker, header, thread chunks, per-thread (names, events) pairs, end
// -- every chunk's CRC valid, every event's name resolved through the names chunk before it,
// and a frame() event's null pointer accepted with no names entry); dumps of a wrapped ring
// while a thread keeps marking (every dump parses, CRCs valid, ticks never go backwards
// within a thread); the chunk splits -- an events chunk ends at 4096 events and when its
// 256-entry name table is full, each events chunk preceded by its names chunk; a wrapped ring
// dumps oldest first with lost counted in its thread chunk; marks made during a dump (the
// recorder's in-flight protocol: left out of the view, never read half-written, and the
// recorder resumes after); a failed dump still resuming; io::FileStream's write/read round
// trip; dump_to_file under PRISM_PROFILING.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/io/byte_stream.hpp>
#include <prism/io/file_stream.hpp>
#include <prism/io/memory_stream.hpp>
#include <prism/profile/dump.hpp>
#include <prism/profile/trace_format.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include <filesystem>
#endif

using prism::profile::Chunk;
using prism::profile::Event;
using prism::profile::Kind;
using prism::profile::Recorder;
using prism::profile::crc32;
using prism::profile::format_version;
using prism::profile::sync_marker;

namespace {

// --- little-endian readers over the dump ---------------------------------------------------

std::uint8_t take_u8(std::span<const std::byte>& s)
{
    REQUIRE(s.size() >= 1);
    const std::uint8_t v = static_cast<std::uint8_t>(s[0]);
    s = s.subspan(1);
    return v;
}

std::uint16_t take_u16(std::span<const std::byte>& s)
{
    REQUIRE(s.size() >= 2);
    const std::uint16_t v = static_cast<std::uint16_t>(static_cast<std::uint16_t>(s[0]) |
                                                       (static_cast<std::uint16_t>(s[1]) << 8));
    s = s.subspan(2);
    return v;
}

std::uint32_t take_u32(std::span<const std::byte>& s)
{
    REQUIRE(s.size() >= 4);
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(s[i]) << (8 * i);
    s = s.subspan(4);
    return v;
}

std::uint64_t take_u64(std::span<const std::byte>& s)
{
    REQUIRE(s.size() >= 8);
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(s[i]) << (8 * i);
    s = s.subspan(8);
    return v;
}

std::int32_t take_i32(std::span<const std::byte>& s) { return static_cast<std::int32_t>(take_u32(s)); }

// --- the chunk reader (test-side only; the library writes, this walks) ----------------------

struct ChunkView {
    Chunk type;
    std::span<const std::byte> payload;
};

// The next chunk of the dump, its CRC verified over type + length + payload.
std::optional<ChunkView> next_chunk(std::span<const std::byte>& rest)
{
    if (rest.empty())
        return std::nullopt;
    REQUIRE(rest.size() >= 5);
    std::span<const std::byte> head = rest;
    const auto type = static_cast<Chunk>(take_u8(head));
    const std::uint32_t length = take_u32(head);
    REQUIRE(head.size() >= static_cast<std::size_t>(length) + 4);
    const std::span<const std::byte> payload = head.subspan(0, length);
    std::span<const std::byte> crc_part = head.subspan(length, 4);
    const std::uint32_t stored_crc = take_u32(crc_part);
    CHECK(crc_part.empty());
    CHECK(crc32(rest.subspan(0, 5 + length)) == stored_crc); // type + length + payload
    rest = rest.subspan(5 + static_cast<std::size_t>(length) + 4);
    return ChunkView{type, payload};
}

// Redefined on purpose instead of using prism::profile::event_wire_size: the parser checks
// the library against an independent constant.
constexpr std::size_t event_wire_size = 8 + 8 + 4 + 1; // ticks, name pointer, value, kind

struct ParsedEvent {
    std::uint64_t ticks;
    std::uint64_t name_ptr;
    std::int32_t value;
    Kind kind;
};

struct ParsedDump {
    std::uint16_t version = 0;
    std::uint8_t pointer_size = 0;
    std::uint64_t ticks_per_second = 0;
    std::uint64_t thread_count = 0;
    std::uint64_t threads_dropped = 0;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::string>> threads; // id -> {lost, name}
    // Per names chunk, in dump order: how many names it carried (one names chunk before each
    // events chunk).
    std::vector<std::size_t> names_chunk_sizes;
    // Per events chunk, in dump order: the thread id and its events, oldest first.
    std::vector<std::pair<std::uint64_t, std::vector<ParsedEvent>>> event_chunks;
    std::map<std::uint64_t, std::string> names_by_ptr; // every name pointer seen -> its string
};

// Walks a whole dump, checking the sync marker, the framing and every chunk's CRC. Chunk order
// per the format: header, one thread chunk per thread, then per thread one or more
// (names, events) pairs, then end.
ParsedDump parse_dump(std::span<const std::byte> bytes)
{
    REQUIRE(bytes.size() >= sync_marker.size());
    CHECK(std::equal(sync_marker.begin(), sync_marker.end(), bytes.begin()));
    std::span<const std::byte> rest = bytes.subspan(sync_marker.size());

    ParsedDump d;
    auto c = next_chunk(rest);
    REQUIRE(c.has_value());
    CHECK(c->type == Chunk::header);
    {
        auto p = c->payload;
        d.version = take_u16(p);
        d.pointer_size = take_u8(p);
        d.ticks_per_second = take_u64(p);
        d.thread_count = take_u64(p);
        d.threads_dropped = take_u64(p);
        CHECK(p.empty());
    }
    for (std::uint64_t i = 0; i < d.thread_count; ++i) {
        c = next_chunk(rest);
        REQUIRE(c.has_value());
        CHECK(c->type == Chunk::thread);
        auto p = c->payload;
        const std::uint64_t id = take_u64(p);
        const std::uint64_t lost = take_u64(p);
        const std::size_t name_len = take_u8(p);
        REQUIRE(p.size() >= name_len);
        std::string name(reinterpret_cast<const char*>(p.data()), name_len);
        p = p.subspan(name_len);
        CHECK(p.empty());
        d.threads[id] = {lost, name};
    }
    for (;;) {
        c = next_chunk(rest);
        REQUIRE(c.has_value());
        if (c->type == Chunk::end) {
            CHECK(c->payload.empty());
            break;
        }
        CHECK(c->type == Chunk::names);
        std::map<std::uint64_t, std::string> names_chunk;
        {
            auto p = c->payload;
            while (!p.empty()) {
                const std::uint64_t ptr = take_u64(p);
                const std::size_t len = take_u16(p);
                REQUIRE(p.size() >= len);
                std::string s(reinterpret_cast<const char*>(p.data()), len);
                p = p.subspan(len);
                names_chunk[ptr] = s;
                d.names_by_ptr[ptr] = s;
            }
        }
        d.names_chunk_sizes.push_back(names_chunk.size());
        c = next_chunk(rest);
        REQUIRE(c.has_value());
        CHECK(c->type == Chunk::events);
        auto p = c->payload;
        const std::uint64_t thread_id = take_u64(p);
        CHECK(d.threads.contains(thread_id));
        REQUIRE(p.size() % event_wire_size == 0);
        std::vector<ParsedEvent> events;
        while (!p.empty()) {
            ParsedEvent ev;
            ev.ticks = take_u64(p);
            ev.name_ptr = take_u64(p);
            ev.value = take_i32(p);
            ev.kind = static_cast<Kind>(take_u8(p));
            // Resolves through the preceding names chunk -- unless it is 0, a frame boundary,
            // which gets no names entry.
            CHECK((ev.name_ptr == 0 || names_chunk.contains(ev.name_ptr)));
            events.push_back(ev);
        }
        CHECK(events.size() <= prism::profile::max_events_per_chunk);
        d.event_chunks.emplace_back(thread_id, std::move(events));
    }
    CHECK(rest.empty());
    return d;
}

// Within each thread's events (its chunks in dump order, oldest first) the ticks never go
// backwards -- a wrapped view that starts on the in-flight event shows the newest tick first.
void check_ticks_order(const ParsedDump& d)
{
    std::map<std::uint64_t, std::vector<const ParsedEvent*>> by_thread;
    for (const auto& chunk : d.event_chunks)
        for (const auto& ev : chunk.second)
            by_thread[chunk.first].push_back(&ev);
    for (const auto& entry : by_thread)
        for (std::size_t i = 1; i < entry.second.size(); ++i)
            CHECK(entry.second[i - 1]->ticks <= entry.second[i]->ticks);
}

// --- recorder helpers -----------------------------------------------------------------------

std::vector<Recorder::ThreadView> threads_of(const Recorder& r)
{
    std::vector<Recorder::ThreadView> views;
    r.for_each_thread([&](const Recorder::ThreadView& tv) { views.push_back(tv); });
    return views;
}

// A thread's ring, oldest first, as one flat vector.
std::vector<Event> events_of(const Recorder::ThreadView& tv)
{
    std::vector<Event> out(tv.first.begin(), tv.first.end());
    out.insert(out.end(), tv.second.begin(), tv.second.end());
    return out;
}

// A ByteStream whose writes always fail.
struct FailingStream {
    std::expected<void, std::error_code> write(std::span<const std::byte>) {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    std::expected<std::size_t, std::error_code> read_some(std::span<std::byte>) { return std::size_t{0}; }
    void close() {}
};
static_assert(prism::io::ByteStream<FailingStream>);

} // namespace

TEST_CASE("crc32 is the IEEE reflected CRC-32 (known answer)")
{
    CHECK(crc32(prism::io::as_bytes("123456789")) == 0xCBF43926u);
    CHECK(crc32(std::span<const std::byte>{}) == 0u); // init XOR final XOR over no bytes
}

TEST_CASE("a two-thread three-name dump is the framed chunks in order, every CRC valid")
{
    std::vector<Event> storage(256);
    Recorder r{storage, 2}; // 128 events per ring
    r.name_thread("main");
    r.instant("alpha");
    r.counter("heap", 42);
    r.frame(); // a frame boundary: name pointer 0, no names-chunk entry
    std::jthread worker([&] {
        r.name_thread("worker");
        r.begin("beta");
        r.end("beta");
    });
    worker.join();

    prism::io::MemoryStream out;
    const auto res = prism::profile::dump(r, out);
    REQUIRE(res.has_value());

    const ParsedDump d = parse_dump(out.bytes()); // starts with the sync marker, CRCs checked
    CHECK(d.version == format_version);
    CHECK(d.version == 1);
    CHECK(d.pointer_size == sizeof(void*));
    CHECK(d.ticks_per_second == prism::platform::profile_ticks_per_second());
    CHECK(d.thread_count == 2);
    CHECK(d.threads_dropped == 0);

    REQUIRE(d.threads.size() == 2); // one thread chunk per thread
    std::vector<std::string> thread_names;
    for (const auto& entry : d.threads) {
        CHECK(entry.second.first == 0); // lost
        thread_names.push_back(entry.second.second);
    }
    std::sort(thread_names.begin(), thread_names.end());
    CHECK(thread_names == std::vector<std::string>{"main", "worker"});

    REQUIRE(d.event_chunks.size() == 2); // one (names, events) pair per thread

    std::vector<std::string> names; // three distinct names across the whole dump
    for (const auto& entry : d.names_by_ptr) {
        names.push_back(entry.second);
        // The pointer is a live string in this process: the dump stored its strlen, cut at 65535.
        CHECK(std::string_view(reinterpret_cast<const char*>(static_cast<std::uintptr_t>(entry.first))) ==
              entry.second);
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"alpha", "beta", "heap"});

    for (const auto& chunk : d.event_chunks) {
        const std::string& tname = d.threads.at(chunk.first).second;
        const auto& events = chunk.second;
        for (std::size_t i = 1; i < events.size(); ++i)
            CHECK(events[i - 1].ticks <= events[i].ticks); // oldest first
        if (tname == "main") {
            REQUIRE(events.size() == 3);
            CHECK(events[0].kind == Kind::instant);
            CHECK(d.names_by_ptr.at(events[0].name_ptr) == "alpha");
            CHECK(events[0].value == 0);
            CHECK(events[1].kind == Kind::counter);
            CHECK(d.names_by_ptr.at(events[1].name_ptr) == "heap");
            CHECK(events[1].value == 42);
            CHECK(events[2].kind == Kind::frame);
            CHECK(events[2].name_ptr == 0); // no name: the parser accepts it, no names entry
            CHECK(events[2].value == 0);
        } else {
            CHECK(tname == "worker");
            REQUIRE(events.size() == 2);
            CHECK(events[0].kind == Kind::begin);
            CHECK(d.names_by_ptr.at(events[0].name_ptr) == "beta");
            CHECK(events[1].kind == Kind::end);
            CHECK(d.names_by_ptr.at(events[1].name_ptr) == "beta");
        }
    }
    check_ticks_order(d); // oldest first across each thread's chunk(s)
}

TEST_CASE("marks during a dump are dropped and resume after")
{
    std::vector<Event> storage(262144);
    Recorder r{storage, 2}; // 131072 per ring: no wrap while this test runs
    r.name_thread("main");
    r.instant("before");

    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::jthread marker([&] {
        r.name_thread("marker");
        r.instant("spin");
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_relaxed))
            r.instant("spin"); // marks in a loop while the dump runs on the main thread
    });
    while (!started.load(std::memory_order_acquire))
        std::this_thread::yield();

    prism::io::MemoryStream out;
    const auto res = prism::profile::dump(r, out); // pauses the recorder while it runs
    stop.store(true, std::memory_order_release);
    marker.join();
    REQUIRE(res.has_value());

    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 2);
    REQUIRE(d.event_chunks.size() == 2);
    std::map<std::uint64_t, std::vector<ParsedEvent>> by_thread;
    std::size_t dumped_total = 0;
    for (const auto& chunk : d.event_chunks) {
        for (std::size_t i = 1; i < chunk.second.size(); ++i)
            CHECK(chunk.second[i - 1].ticks <= chunk.second[i].ticks); // oldest first, nothing half-written
        for (const auto& ev : chunk.second)
            CHECK(ev.kind <= Kind::frame); // well-formed
        auto& acc = by_thread[chunk.first];
        acc.insert(acc.end(), chunk.second.begin(), chunk.second.end());
        dumped_total += chunk.second.size();
    }
    CHECK(dumped_total >= 2); // "before" on main, at least one "spin" on the marker

    // The dumped events are what the rings held at the pause -- a prefix of the rings now --
    // and the recorder resumed after the dump: a new mark lands.
    r.instant("after");
    for (const auto& tv : threads_of(r)) {
        const auto events = events_of(tv);
        const auto it = by_thread.find(tv.id);
        REQUIRE(it != by_thread.end());
        const auto& dumped = it->second;
        REQUIRE(events.size() >= dumped.size());
        for (std::size_t i = 0; i < dumped.size(); ++i)
            CHECK(events[i].ticks == dumped[i].ticks); // the dump is a prefix of the ring
        if (tv.name == "main") {
            REQUIRE(events.size() == dumped.size() + 1); // "before" was dumped, "after" is new
            CHECK(std::string_view(events.back().name) == "after");
        }
    }
}

TEST_CASE("an events chunk splits at 4096 events")
{
    std::vector<Event> storage(10000);
    Recorder r{storage, 1}; // one ring of 10000 events
    for (int i = 0; i < 10000; ++i)
        r.instant("tick");
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.event_chunks.size() == 3); // 4096 + 4096 + 1808
    CHECK(d.event_chunks[0].second.size() == 4096);
    CHECK(d.event_chunks[1].second.size() == 4096);
    CHECK(d.event_chunks[2].second.size() == 1808);
    // Each events chunk is preceded by its own names chunk (here: the one name, again).
    CHECK(d.names_chunk_sizes == std::vector<std::size_t>{1, 1, 1});
    const std::uint64_t id = d.event_chunks[0].first;
    CHECK(d.event_chunks[1].first == id);
    CHECK(d.event_chunks[2].first == id);
    REQUIRE(d.threads.size() == 1);
    CHECK(d.threads.begin()->second.first == 0); // lost: 10000 marks into a 10000 ring keeps all
    check_ticks_order(d); // oldest first, across the chunk seams too
    for (const auto& chunk : d.event_chunks)
        for (const auto& ev : chunk.second) {
            CHECK(ev.kind == Kind::instant);
            CHECK(d.names_by_ptr.at(ev.name_ptr) == "tick"); // resolved through its names chunk
        }
}

TEST_CASE("an events chunk splits when its name table is full")
{
    std::vector<Event> storage(512);
    Recorder r{storage, 1}; // one ring, no wrap
    std::array<std::string, 300> names; // stable addresses: names are recorded by pointer
    for (int i = 0; i < 300; ++i) {
        names[std::size_t(i)] = "n" + std::to_string(i);
        r.instant(names[std::size_t(i)].c_str());
    }
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.event_chunks.size() == 2);
    CHECK(d.event_chunks[0].second.size() == 256); // the 257th name would overflow the table
    CHECK(d.event_chunks[1].second.size() == 44);
    CHECK(d.names_chunk_sizes == std::vector<std::size_t>{256, 44});
    std::size_t flat = 0; // oldest first: the events are n0, n1, ... in order
    for (const auto& chunk : d.event_chunks)
        for (const auto& ev : chunk.second) {
            CHECK(d.names_by_ptr.at(ev.name_ptr) == names[flat]); // through its own names chunk
            ++flat;
        }
    CHECK(flat == 300);
    check_ticks_order(d);
}

TEST_CASE("a wrapped ring dumps oldest first")
{
    std::vector<Event> storage(16);
    Recorder r{storage, 1}; // one 16-event ring
    std::array<std::string, 20> names; // stable addresses: names are recorded by pointer
    for (int i = 0; i < 20; ++i) {
        names[std::size_t(i)] = "e" + std::to_string(i);
        r.instant(names[std::size_t(i)].c_str());
    }
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.threads.size() == 1);
    CHECK(d.threads.begin()->second.first == 4); // the thread chunk's lost: 20 recorded, 16 kept
    REQUIRE(d.event_chunks.size() == 1);
    const auto& events = d.event_chunks[0].second;
    REQUIRE(events.size() == 16);
    for (std::size_t i = 0; i < events.size(); ++i)
        CHECK(d.names_by_ptr.at(events[i].name_ptr) == names[i + 4]); // the last 16, in order
    check_ticks_order(d);
}

TEST_CASE("dumps of a wrapped ring while a thread keeps marking")
{
    // The I1/I2 probe: with a ring this small it wraps constantly, so every dump races the
    // marker. Today this fails under ThreadSanitizer (resume() is a relaxed store, so a mark
    // right after a dump can overwrite an event the dump just read, with no synchronizes-with
    // edge) and can fail on the tick order (head is loaded before writing, so a wrapped view
    // can start on the in-flight -- newest -- event).
    std::vector<Event> storage(16);
    Recorder r{storage, 1}; // one 16-event ring
    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::jthread marker([&] {
        r.instant("spin");
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_relaxed))
            r.instant("spin");
    });
    while (!started.load(std::memory_order_acquire))
        std::this_thread::yield();

    for (int i = 0; i < 200; ++i) {
        prism::io::MemoryStream out;
        const auto res = prism::profile::dump(r, out); // pauses: an in-flight mark is left out
        REQUIRE(res.has_value());
        const ParsedDump d = parse_dump(out.bytes()); // sync marker, framing, every CRC valid
        CHECK(d.thread_count == 1);
        REQUIRE(d.event_chunks.size() == 1);
        CHECK(!d.event_chunks[0].second.empty());
        CHECK(d.event_chunks[0].second.size() <= 16);
        check_ticks_order(d); // within the thread's events the ticks never go backwards
    }
    stop.store(true, std::memory_order_release);
    marker.join(); // joined before the end: no mark runs past the last dump

    prism::io::MemoryStream out; // after all that the recorder still dumps cleanly
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.event_chunks.size() == 1);
    CHECK(d.event_chunks[0].second.size() == 16); // the ring has long since wrapped
    check_ticks_order(d);
}

TEST_CASE("a failed dump reports the error and still resumes the recorder")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 2};
    r.instant("x");
    FailingStream s;
    const auto res = prism::profile::dump(r, s);
    CHECK(!res.has_value());
    CHECK(res.error() == std::errc::io_error);
    r.instant("y"); // the dump resumed the recorder on its error path: this mark lands
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 2);
    CHECK(std::string_view(events[1].name) == "y");
}

#if !defined(__EMSCRIPTEN__)
TEST_CASE("io::FileStream writes then reads back the same bytes")
{
    const auto path = std::filesystem::temp_directory_path() / "prism-profile-dump-stream.bin";
    std::vector<std::byte> payload(1000);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::byte>((i * 31 + 7) & 0xFF);
    {
        auto file = prism::io::FileStream::open(path.string(), prism::io::FileStream::Mode::write);
        REQUIRE(file.has_value());
        CHECK(file->write(std::span{payload}).has_value());
        file->close();
    }
    {
        auto file = prism::io::FileStream::open(path.string(), prism::io::FileStream::Mode::read);
        REQUIRE(file.has_value());
        std::vector<std::byte> back;
        std::array<std::byte, 64> buf{};
        for (;;) {
            auto got = file->read_some(std::span{buf});
            REQUIRE(got.has_value());
            if (*got == 0)
                break; // EOF: the stream is closed for reading
            back.insert(back.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*got));
        }
        CHECK(back == payload);
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

#if defined(PRISM_PROFILING)
TEST_CASE("dump_to_file writes a parseable dump of the global recorder")
{
    const auto path = std::filesystem::temp_directory_path() / "prism-profile-global.bin";
    const auto res = prism::profile::dump_to_file(path.string());
    REQUIRE(res.has_value());
    auto file = prism::io::FileStream::open(path.string(), prism::io::FileStream::Mode::read);
    REQUIRE(file.has_value());
    std::vector<std::byte> bytes;
    std::array<std::byte, 256> buf{};
    for (;;) {
        auto got = file->read_some(std::span{buf});
        REQUIRE(got.has_value());
        if (*got == 0)
            break;
        bytes.insert(bytes.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(*got));
    }
    const ParsedDump d = parse_dump(bytes);
    CHECK(d.version == 1);
    CHECK(d.pointer_size == sizeof(void*));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
#endif
#endif // !__EMSCRIPTEN__
