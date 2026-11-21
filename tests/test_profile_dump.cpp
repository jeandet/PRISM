// The binary dump (include/prism/profile/dump.hpp + trace_format.hpp over any io::ByteStream,
// doc/design/profiling.md): crc32's known answer; the framed chunk layout of a two-thread dump
// (sync marker, header, thread chunks, per-thread (sites, events) pairs, end -- every chunk's
// CRC valid, every event's site resolved through the sites chunk before it, and a frame()
// event pointing at frame()'s own site, not a null pointer); a dumped site carries its name,
// function, file and line; dumps of a wrapped ring while a thread keeps marking (every dump
// parses, CRCs valid, ticks never go backwards within a thread); the chunk splits -- an
// events chunk ends at 4096 events and when its 256-entry site table is full, each events
// chunk preceded by its sites chunk; a wrapped ring dumps oldest first with lost counted in
// its thread chunk; marks made during a dump (the recorder's in-flight protocol: left out of
// the view, never read half-written, and the recorder resumes after); a failed dump still
// resuming; io::FileStream's write/read round trip; dump_to_file under PRISM_PROFILING.
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
using prism::profile::Site;
using prism::profile::crc32;
using prism::profile::format_version;
using prism::profile::make_site;
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
constexpr std::size_t event_wire_size = 8 + 8 + 4 + 1; // ticks, site pointer, value, kind

struct ParsedEvent {
    std::uint64_t ticks;
    std::uint64_t site_ptr;
    std::int32_t value;
    Kind kind;
};

// One entry of a sites chunk: pointer u64 | line u32 | name, function, file (each u16 + bytes).
struct ParsedSite {
    std::string name;
    std::string function;
    std::string file;
    std::uint32_t line = 0;
};

struct ParsedDump {
    std::uint16_t version = 0;
    std::uint8_t pointer_size = 0;
    std::uint64_t ticks_per_second = 0;
    std::uint64_t thread_count = 0;
    std::uint64_t threads_dropped = 0;
    std::map<std::uint64_t, std::pair<std::uint64_t, std::string>> threads; // id -> {lost, name}
    // Per sites chunk, in dump order: how many sites it carried (one sites chunk before each
    // events chunk).
    std::vector<std::size_t> sites_chunk_sizes;
    // Per events chunk, in dump order: the thread id and its events, oldest first.
    std::vector<std::pair<std::uint64_t, std::vector<ParsedEvent>>> event_chunks;
    std::map<std::uint64_t, ParsedSite> sites_by_ptr; // every site pointer seen -> its fields
};

// Walks a whole dump, checking the sync marker, the framing and every chunk's CRC. Chunk order
// per the format: header, one thread chunk per thread, then per thread one or more
// (sites, events) pairs, then end.
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
        CHECK(c->type == Chunk::sites);
        std::map<std::uint64_t, ParsedSite> sites_chunk;
        {
            auto p = c->payload;
            while (!p.empty()) {
                const std::uint64_t ptr = take_u64(p);
                ParsedSite s;
                s.line = take_u32(p);
                auto take_string = [&p] {
                    const std::size_t len = take_u16(p);
                    REQUIRE(p.size() >= len);
                    std::string str(reinterpret_cast<const char*>(p.data()), len);
                    p = p.subspan(len);
                    return str;
                };
                s.name = take_string();
                s.function = take_string();
                s.file = take_string();
                sites_chunk[ptr] = s;
                d.sites_by_ptr[ptr] = s;
            }
            CHECK(p.empty());
        }
        d.sites_chunk_sizes.push_back(sites_chunk.size());
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
            ev.site_ptr = take_u64(p);
            ev.value = take_i32(p);
            ev.kind = static_cast<Kind>(take_u8(p));
            // Every event points at a site -- frame boundaries have their own, so the
            // pointer is never 0 -- resolved through the preceding sites chunk.
            CHECK(sites_chunk.contains(ev.site_ptr));
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

// n distinct sites whose names stay alive: sites are recorded by pointer, so the names must
// outlive the recorder's use of them. Built at runtime -- only the macros need a constexpr
// site.
struct SiteArray {
    std::vector<std::string> names;
    std::vector<Site> sites;

    explicit SiteArray(std::size_t n, const std::string& prefix) : names(n), sites(n)
    {
        for (std::size_t i = 0; i < n; ++i) {
            names[i] = prefix + std::to_string(i);
            sites[i] = Site{names[i].c_str(), "test_fn", "test_profile_dump.cpp",
                            static_cast<std::uint32_t>(i + 1)};
        }
    }
};

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

TEST_CASE("a two-thread dump is the framed chunks in order, every CRC valid")
{
    std::vector<Event> storage(256);
    Recorder r{storage, 2}; // 128 events per ring
    static constexpr Site alpha = make_site("alpha");
    static constexpr Site heap = make_site("heap");
    static constexpr Site beta = make_site("beta");
    r.name_thread("main");
    r.instant(alpha);
    r.counter(heap, 42);
    r.frame(); // a frame boundary: frame()'s own site, not a null pointer
    std::jthread worker([&] {
        r.name_thread("worker");
        r.begin(beta);
        r.end(beta);
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

    REQUIRE(d.event_chunks.size() == 2); // one (sites, events) pair per thread

    std::vector<std::string> site_names; // four distinct sites across the whole dump
    for (const auto& entry : d.sites_by_ptr) {
        site_names.push_back(entry.second.name);
        // The pointer is a live Site in this process: the dump stored its strings and line.
        const Site* s = reinterpret_cast<const Site*>(static_cast<std::uintptr_t>(entry.first));
        CHECK(std::string_view(s->name) == entry.second.name);
        CHECK(std::string_view(s->function) == entry.second.function);
        CHECK(std::string_view(s->file) == entry.second.file);
        CHECK(s->line == entry.second.line);
    }
    std::sort(site_names.begin(), site_names.end());
    CHECK(site_names == std::vector<std::string>{"alpha", "beta", "frame", "heap"});

    for (const auto& chunk : d.event_chunks) {
        const std::string& tname = d.threads.at(chunk.first).second;
        const auto& events = chunk.second;
        for (std::size_t i = 1; i < events.size(); ++i)
            CHECK(events[i - 1].ticks <= events[i].ticks); // oldest first
        if (tname == "main") {
            REQUIRE(events.size() == 3);
            CHECK(events[0].kind == Kind::instant);
            CHECK(d.sites_by_ptr.at(events[0].site_ptr).name == "alpha");
            CHECK(events[0].value == 0);
            CHECK(events[1].kind == Kind::counter);
            CHECK(d.sites_by_ptr.at(events[1].site_ptr).name == "heap");
            CHECK(events[1].value == 42);
            CHECK(events[2].kind == Kind::frame);
            CHECK(d.sites_by_ptr.at(events[2].site_ptr).name == "frame"); // frame()'s own site
            CHECK(events[2].value == 0);
        } else {
            CHECK(tname == "worker");
            REQUIRE(events.size() == 2);
            CHECK(events[0].kind == Kind::begin);
            CHECK(d.sites_by_ptr.at(events[0].site_ptr).name == "beta");
            CHECK(events[1].kind == Kind::end);
            CHECK(d.sites_by_ptr.at(events[1].site_ptr).name == "beta");
        }
    }
    check_ticks_order(d); // oldest first across each thread's chunk(s)
}

TEST_CASE("a dumped site carries its name, function, file and line")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1};
    constexpr std::uint32_t here_line = __LINE__ + 1;
    static constexpr Site here = make_site("here");
    r.instant(here);
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.sites_by_ptr.size() == 1);
    const auto& entry = *d.sites_by_ptr.begin();
    CHECK(entry.first == reinterpret_cast<std::uintptr_t>(&here));
    const ParsedSite& s = entry.second;
    CHECK(s.name == "here");
    CHECK(s.file.ends_with("test_profile_dump.cpp"));
    CHECK(s.file == here.file);
    CHECK(s.line == here_line);
    CHECK(!s.function.empty());
    CHECK(s.function == here.function);
    REQUIRE(d.event_chunks.size() == 1);
    REQUIRE(d.event_chunks[0].second.size() == 1);
    CHECK(d.event_chunks[0].second[0].site_ptr == reinterpret_cast<std::uintptr_t>(&here));
    CHECK(d.event_chunks[0].second[0].kind == Kind::instant);
}

TEST_CASE("marks during a dump are dropped and resume after")
{
    std::vector<Event> storage(262144);
    Recorder r{storage, 2}; // 131072 per ring: no wrap while this test runs
    static constexpr Site before = make_site("before");
    static constexpr Site spin = make_site("spin");
    static constexpr Site after = make_site("after");
    r.name_thread("main");
    r.instant(before);

    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::jthread marker([&] {
        r.name_thread("marker");
        r.instant(spin);
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_relaxed))
            r.instant(spin); // marks in a loop while the dump runs on the main thread
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
    REQUIRE(d.event_chunks.size() >= 2); // a chunk holds at most 4096 events: the marker may need several
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
    CHECK(by_thread.size() == 2);
    CHECK(dumped_total >= 2); // "before" on main, at least one "spin" on the marker

    // The dumped events are what the rings held at the pause -- a prefix of the rings now --
    // and the recorder resumed after the dump: a new mark lands.
    r.instant(after);
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
            CHECK(events.back().site == &after);
            CHECK(std::string_view(events.back().site->name) == "after");
        }
    }
}

TEST_CASE("an events chunk splits at 4096 events")
{
    std::vector<Event> storage(10000);
    Recorder r{storage, 1}; // one ring of 10000 events
    static constexpr Site tick = make_site("tick");
    for (int i = 0; i < 10000; ++i)
        r.instant(tick);
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.event_chunks.size() == 3); // 4096 + 4096 + 1808
    CHECK(d.event_chunks[0].second.size() == 4096);
    CHECK(d.event_chunks[1].second.size() == 4096);
    CHECK(d.event_chunks[2].second.size() == 1808);
    // Each events chunk is preceded by its own sites chunk (here: the one site, again).
    CHECK(d.sites_chunk_sizes == std::vector<std::size_t>{1, 1, 1});
    const std::uint64_t id = d.event_chunks[0].first;
    CHECK(d.event_chunks[1].first == id);
    CHECK(d.event_chunks[2].first == id);
    REQUIRE(d.threads.size() == 1);
    CHECK(d.threads.begin()->second.first == 0); // lost: 10000 marks into a 10000 ring keeps all
    check_ticks_order(d); // oldest first, across the chunk seams too
    for (const auto& chunk : d.event_chunks)
        for (const auto& ev : chunk.second) {
            CHECK(ev.kind == Kind::instant);
            CHECK(d.sites_by_ptr.at(ev.site_ptr).name == "tick"); // resolved through its sites chunk
        }
}

TEST_CASE("an events chunk splits when its site table is full")
{
    std::vector<Event> storage(512);
    Recorder r{storage, 1}; // one ring, no wrap
    SiteArray sites{300, "n"};
    for (std::size_t i = 0; i < sites.sites.size(); ++i)
        r.instant(sites.sites[i]);
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.event_chunks.size() == 2);
    CHECK(d.event_chunks[0].second.size() == 256); // the 257th site would overflow the table
    CHECK(d.event_chunks[1].second.size() == 44);
    CHECK(d.sites_chunk_sizes == std::vector<std::size_t>{256, 44});
    std::size_t flat = 0; // oldest first: the events are n0, n1, ... in order
    for (const auto& chunk : d.event_chunks)
        for (const auto& ev : chunk.second) {
            CHECK(ev.site_ptr == reinterpret_cast<std::uintptr_t>(&sites.sites[flat]));
            CHECK(d.sites_by_ptr.at(ev.site_ptr).name == sites.names[flat]); // through its own sites chunk
            ++flat;
        }
    CHECK(flat == 300);
    check_ticks_order(d);
}

TEST_CASE("a wrapped ring dumps oldest first")
{
    std::vector<Event> storage(16);
    Recorder r{storage, 1}; // one 16-event ring
    SiteArray sites{20, "e"};
    for (std::size_t i = 0; i < sites.sites.size(); ++i)
        r.instant(sites.sites[i]);
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    const ParsedDump d = parse_dump(out.bytes());
    CHECK(d.thread_count == 1);
    REQUIRE(d.threads.size() == 1);
    CHECK(d.threads.begin()->second.first == 4); // the thread chunk's lost: 20 recorded, 16 kept
    REQUIRE(d.event_chunks.size() == 1);
    const auto& events = d.event_chunks[0].second;
    REQUIRE(events.size() == 16);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].site_ptr == reinterpret_cast<std::uintptr_t>(&sites.sites[i + 4]));
        CHECK(d.sites_by_ptr.at(events[i].site_ptr).name == sites.names[i + 4]); // the last 16, in order
    }
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
    static constexpr Site spin = make_site("spin");
    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    std::jthread marker([&] {
        r.instant(spin);
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_relaxed))
            r.instant(spin);
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
    static constexpr Site x = make_site("x");
    static constexpr Site y = make_site("y");
    r.instant(x);
    FailingStream s;
    const auto res = prism::profile::dump(r, s);
    CHECK(!res.has_value());
    CHECK(res.error() == std::errc::io_error);
    r.instant(y); // the dump resumed the recorder on its error path: this mark lands
    const auto threads = threads_of(r);
    REQUIRE(threads.size() == 1);
    const auto events = events_of(threads[0]);
    REQUIRE(events.size() == 2);
    CHECK(events[1].site == &y);
    CHECK(std::string_view(events[1].site->name) == "y");
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
