// The prism-trace host tool (tools/prism-trace): read_trace over real dumps -- Recorder +
// make_site + prism::profile::dump into io::MemoryStream -- and to_chrome_json, checked through
// Glaze's generic JSON read (glz::generic; this Glaze, 9.1.0, has glz::generic, with json_t a
// deprecated alias of it). Round trip (two threads, a block, an instant, a counter, a frame);
// a byte flipped inside an events chunk (that chunk skipped, one warning, the other threads
// still read); garbage bytes before the sync marker and after the end chunk (ignored); a dump
// with no end chunk (a warning, not an error); a ring that wrapped mid-block (valid JSON, the
// orphan E dropped, the open B closed at the thread's last timestamp); a hand-built dump with
// pointer size 4 (site pointers are raw u64 bits, never reinterpreted).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism-trace/chrome_json.hpp>
#include <prism-trace/trace_reader.hpp>

#include <prism/io/memory_stream.hpp>
#include <prism/profile/dump.hpp>
#include <prism/profile/recorder.hpp>
#include <prism/profile/trace_format.hpp>

#include <glaze/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using prism::profile::Chunk;
using prism::profile::Event;
using prism::profile::Kind;
using prism::profile::Recorder;
using prism::profile::Site;
using prism::profile::make_site;
using prism_trace::SiteInfo;
using prism_trace::Trace;
using prism_trace::read_trace;
using prism_trace::to_chrome_json;

namespace {

// --- little-endian writers for the hand-built dump -------------------------------------------

void put_u8(std::vector<std::byte>& v, std::uint8_t x) { v.push_back(static_cast<std::byte>(x)); }

void put_u16(std::vector<std::byte>& v, std::uint16_t x)
{
    for (std::size_t i = 0; i < 2; ++i)
        v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFFu));
}

void put_u32(std::vector<std::byte>& v, std::uint32_t x)
{
    for (std::size_t i = 0; i < 4; ++i)
        v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFFu));
}

void put_u64(std::vector<std::byte>& v, std::uint64_t x)
{
    for (std::size_t i = 0; i < 8; ++i)
        v.push_back(static_cast<std::byte>((x >> (8 * i)) & 0xFFu));
}

void put_bytes(std::vector<std::byte>& v, std::string_view s)
{
    for (const char c : s)
        v.push_back(static_cast<std::byte>(c));
}

// A sites-chunk string: u16 length + bytes.
void put_string16(std::vector<std::byte>& v, std::string_view s)
{
    put_u16(v, static_cast<std::uint16_t>(s.size()));
    put_bytes(v, s);
}

// One framed chunk (trace_format.hpp): type u8 | payload length u32 LE | payload | CRC-32 u32 LE
// of type+length+payload. Redefined on purpose instead of reusing the dump's writer: the tests
// check the tool against an independent writer.
void append_chunk(std::vector<std::byte>& out, Chunk type, std::span<const std::byte> payload)
{
    std::array<std::byte, 5> head{};
    head[0] = static_cast<std::byte>(type);
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    for (std::size_t i = 0; i < 4; ++i)
        head[1 + i] = static_cast<std::byte>((length >> (8 * i)) & 0xFFu);
    std::vector<std::byte> crc_input(head.begin(), head.end());
    crc_input.insert(crc_input.end(), payload.begin(), payload.end());
    const std::uint32_t crc = prism::profile::crc32(crc_input);
    out.insert(out.end(), head.begin(), head.end());
    out.insert(out.end(), payload.begin(), payload.end());
    for (std::size_t i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>((crc >> (8 * i)) & 0xFFu));
}

// --- chunk walking, for the tests that damage bytes ------------------------------------------

struct ChunkSpan {
    Chunk type;
    std::size_t header;  // offset of the type byte
    std::size_t payload; // offset of the first payload byte
    std::uint32_t length;
};

std::size_t find_sync_marker(std::span<const std::byte> bytes)
{
    for (std::size_t i = 0; i + prism::profile::sync_marker.size() <= bytes.size(); ++i) {
        bool match = true;
        for (std::size_t k = 0; k < prism::profile::sync_marker.size(); ++k)
            if (bytes[i + k] != prism::profile::sync_marker[k]) {
                match = false;
                break;
            }
        if (match)
            return i;
    }
    return bytes.size();
}

// The chunk layout of a dump: sync marker, then type u8 | length u32 | payload | crc u32 per
// chunk, up to the first truncated tail.
std::vector<ChunkSpan> walk_chunks(std::span<const std::byte> bytes)
{
    std::vector<ChunkSpan> chunks;
    std::size_t pos = find_sync_marker(bytes);
    if (pos == bytes.size())
        return chunks;
    pos += prism::profile::sync_marker.size();
    while (pos + 5 <= bytes.size()) {
        const auto type = static_cast<Chunk>(static_cast<std::uint8_t>(bytes[pos]));
        std::uint32_t length = 0;
        for (std::size_t i = 0; i < 4; ++i)
            length |= static_cast<std::uint32_t>(bytes[pos + 1 + i]) << (8 * i);
        if (pos + 5 + static_cast<std::size_t>(length) + 4 > bytes.size())
            break;
        chunks.push_back({type, pos, pos + 5, length});
        pos += 5 + static_cast<std::size_t>(length) + 4;
    }
    return chunks;
}

// --- recorder helpers -------------------------------------------------------------------------

// Dumps r into a MemoryStream and returns the bytes.
std::vector<std::byte> dumped(Recorder& r)
{
    prism::io::MemoryStream out;
    REQUIRE(prism::profile::dump(r, out).has_value());
    return std::vector<std::byte>(out.bytes().begin(), out.bytes().end());
}

std::uint64_t ptr_of(const Site& site) { return reinterpret_cast<std::uintptr_t>(&site); }

// --- JSON helpers (Glaze's generic read) -------------------------------------------------------

glz::generic parse_json(const std::string& json)
{
    auto parsed = glz::read_json<glz::generic>(json);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

const glz::generic& field(const glz::generic& obj, std::string_view key)
{
    REQUIRE(obj.is_object());
    REQUIRE(obj.contains(key));
    return obj[key];
}

double num_field(const glz::generic& obj, std::string_view key)
{
    const glz::generic& v = field(obj, key);
    REQUIRE(v.is_number());
    return v.as<double>();
}

std::string str_field(const glz::generic& obj, std::string_view key)
{
    const glz::generic& v = field(obj, key);
    REQUIRE(v.is_string());
    return v.get<std::string>();
}

// The traceEvents entries with the given "ph" (and, when `name` is not empty, "name").
std::vector<const glz::generic*> events_with(const glz::generic& root, std::string_view ph,
                                             std::string_view name = {})
{
    const glz::generic& arr = field(root, "traceEvents");
    REQUIRE(arr.is_array());
    std::vector<const glz::generic*> out;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const glz::generic& ev = arr[i];
        if (str_field(ev, "ph") != ph)
            continue;
        if (!name.empty() && str_field(ev, "name") != name)
            continue;
        out.push_back(&ev);
    }
    return out;
}

// ts in microseconds, as the JSON carries it.
double ts_us(const Trace& trace, std::uint64_t ticks)
{
    return static_cast<double>(ticks) * 1e6 / trace.ticks_per_second;
}

} // namespace

TEST_CASE("round trip: two threads, a block, an instant, a counter, a frame -> Chrome JSON")
{
    std::vector<Event> storage(256);
    Recorder r{storage, 2}; // 128 events per ring: no wrap
    static constexpr Site block = make_site("block");
    static constexpr Site mark = make_site("mark");
    static constexpr Site heap = make_site("heap");
    r.name_thread("main");
    r.instant(mark);
    r.counter(heap, 123);
    r.frame();
    std::jthread worker([&] {
        r.name_thread("worker");
        r.begin(block);
        r.end(block);
    });
    worker.join();

    const auto bytes = dumped(r);
    const Trace trace = read_trace(bytes);
    CHECK(trace.warnings.empty());
    CHECK(trace.threads_dropped == 0);
    CHECK(trace.ticks_per_second == static_cast<double>(prism::platform::profile_ticks_per_second()));
    REQUIRE(trace.threads.size() == 2);

    const Trace::Thread* main_t = nullptr;
    const Trace::Thread* worker_t = nullptr;
    for (const Trace::Thread& t : trace.threads) {
        if (t.name == "main")
            main_t = &t;
        if (t.name == "worker")
            worker_t = &t;
    }
    REQUIRE(main_t != nullptr);
    REQUIRE(worker_t != nullptr);
    CHECK(main_t->lost == 0);
    CHECK(worker_t->lost == 0);
    REQUIRE(main_t->events.size() == 3); // instant, counter, frame
    CHECK(main_t->events[0].kind == Kind::instant);
    CHECK(main_t->events[0].site == ptr_of(mark));
    CHECK(main_t->events[1].kind == Kind::counter);
    CHECK(main_t->events[1].site == ptr_of(heap));
    CHECK(main_t->events[1].value == 123);
    CHECK(main_t->events[2].kind == Kind::frame);
    for (std::size_t i = 1; i < main_t->events.size(); ++i)
        CHECK(main_t->events[i - 1].ticks <= main_t->events[i].ticks); // oldest first
    REQUIRE(worker_t->events.size() == 2); // the block
    CHECK(worker_t->events[0].kind == Kind::begin);
    CHECK(worker_t->events[1].kind == Kind::end);
    CHECK(worker_t->events[0].site == ptr_of(block));
    CHECK(worker_t->events[1].site == ptr_of(block));

    // The sites, merged by pointer: block, mark, heap and frame()'s own site.
    REQUIRE(trace.sites.size() == 4);
    const SiteInfo& block_info = trace.sites.at(ptr_of(block));
    CHECK(block_info.name == "block");
    CHECK(block_info.function == block.function);
    CHECK(block_info.file == block.file);
    CHECK(block_info.line == block.line);
    CHECK(trace.sites.at(ptr_of(mark)).name == "mark");
    CHECK(trace.sites.at(ptr_of(heap)).name == "heap");
    CHECK(trace.sites.at(main_t->events[2].site).name == "frame"); // the frame boundary's site

    // The JSON parses and carries what the brief asks for.
    const glz::generic root = parse_json(to_chrome_json(trace));
    const glz::generic& arr = field(root, "traceEvents");
    REQUIRE(arr.is_array());
    CHECK(arr.size() == 7); // B, E, i(mark), C(heap), i(frame), M, M

    // Thread names as "M" thread_name metadata, on their thread's tid.
    const auto m = events_with(root, "M");
    REQUIRE(m.size() == 2);
    std::vector<std::string> names;
    for (const glz::generic* ev : m) {
        CHECK(str_field(*ev, "name") == "thread_name");
        CHECK(num_field(*ev, "pid") == 1);
        const std::string thread_name = str_field(field(*ev, "args"), "name");
        names.push_back(thread_name);
        CHECK(num_field(*ev, "tid") ==
              static_cast<double>(thread_name == "main" ? main_t->id : worker_t->id));
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"main", "worker"});

    // The block: one B and one E on the worker's tid, named after the site, ts in microseconds,
    // args carrying the site's function, file and line.
    const auto b = events_with(root, "B");
    REQUIRE(b.size() == 1);
    CHECK(str_field(*b[0], "name") == "block");
    CHECK(num_field(*b[0], "pid") == 1);
    CHECK(num_field(*b[0], "tid") == static_cast<double>(worker_t->id));
    CHECK(num_field(*b[0], "ts") == doctest::Approx(ts_us(trace, worker_t->events[0].ticks)));
    const glz::generic& bargs = field(*b[0], "args");
    CHECK(str_field(bargs, "function") == block.function);
    CHECK(str_field(bargs, "file") == block.file);
    CHECK(num_field(bargs, "line") == static_cast<double>(block.line)); // args.line is the site's line
    const auto e = events_with(root, "E");
    REQUIRE(e.size() == 1);
    CHECK(str_field(*e[0], "name") == "block");
    CHECK(num_field(*e[0], "tid") == static_cast<double>(worker_t->id));
    CHECK(num_field(*e[0], "ts") == doctest::Approx(ts_us(trace, worker_t->events[1].ticks)));

    // The instant and the frame boundary: "i" with "s":"t", on the main thread's tid.
    const auto i = events_with(root, "i");
    REQUIRE(i.size() == 2);
    std::vector<std::string> inames;
    const glz::generic* mark_ev = nullptr;
    for (const glz::generic* ev : i) {
        CHECK(num_field(*ev, "tid") == static_cast<double>(main_t->id));
        CHECK(str_field(*ev, "s") == "t");
        const std::string name = str_field(*ev, "name");
        inames.push_back(name);
        if (name == "mark")
            mark_ev = ev;
    }
    std::sort(inames.begin(), inames.end());
    CHECK(inames == std::vector<std::string>{"frame", "mark"}); // the frame is an instant "frame"
    REQUIRE(mark_ev != nullptr);
    CHECK(num_field(field(*mark_ev, "args"), "line") == static_cast<double>(mark.line));

    // The counter: "C" with args.value.
    const auto c = events_with(root, "C");
    REQUIRE(c.size() == 1);
    CHECK(str_field(*c[0], "name") == "heap");
    CHECK(num_field(*c[0], "tid") == static_cast<double>(main_t->id));
    CHECK(num_field(field(*c[0], "args"), "value") == 123);
}

TEST_CASE("a byte flipped inside an events chunk: that chunk is skipped, one warning, the other threads still read")
{
    std::vector<Event> storage(256);
    Recorder r{storage, 2};
    static constexpr Site a = make_site("a");
    static constexpr Site b = make_site("b");
    r.name_thread("main");
    r.instant(a);
    r.instant(a);
    std::jthread worker([&] {
        r.name_thread("worker");
        r.instant(b);
        r.instant(b);
    });
    worker.join();
    auto bytes = dumped(r);

    // Flip one payload byte of the first events chunk (the main thread's: the dump writes the
    // threads in slot order, main claimed slot 0 first).
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* events = nullptr;
    for (const ChunkSpan& c : chunks)
        if (c.type == Chunk::events) {
            events = &c;
            break;
        }
    REQUIRE(events != nullptr);
    const std::size_t flip = events->payload + events->length / 2;
    bytes[flip] = static_cast<std::byte>(static_cast<unsigned int>(bytes[flip]) ^ 0xFFu);

    const Trace trace = read_trace(bytes);
    CHECK(trace.warnings.size() == 1); // exactly one warning: the damaged chunk
    REQUIRE(trace.threads.size() == 2);
    // The sites chunks were intact, so both sites are still known...
    CHECK(trace.sites.size() == 2);
    // ...but the damaged thread's events are gone and the other thread's two survived.
    std::size_t total = 0;
    std::size_t empty_threads = 0;
    for (const Trace::Thread& t : trace.threads) {
        total += t.events.size();
        if (t.events.empty())
            ++empty_threads;
        else
            CHECK(t.events.size() == 2);
    }
    CHECK(total == 2);
    CHECK(empty_threads == 1);
    // And the JSON over what survived still parses.
    const glz::generic root = parse_json(to_chrome_json(trace));
    CHECK(events_with(root, "i").size() == 2);
}

TEST_CASE("garbage bytes before the sync marker and after the end chunk are ignored")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1};
    static constexpr Site mark = make_site("mark");
    r.name_thread("main");
    r.instant(mark);
    r.frame();
    const auto bytes = dumped(r);

    std::vector<std::byte> noisy;
    for (int i = 0; i < 10; ++i)
        noisy.push_back(std::byte{0xAB}); // garbage before the sync marker
    noisy.insert(noisy.end(), bytes.begin(), bytes.end());
    for (int i = 0; i < 7; ++i)
        noisy.push_back(std::byte{0x00}); // garbage after the end chunk

    const Trace trace = read_trace(noisy);
    CHECK(trace.warnings.empty());
    REQUIRE(trace.threads.size() == 1);
    CHECK(trace.threads[0].name == "main");
    REQUIRE(trace.threads[0].events.size() == 2);
    CHECK(trace.threads[0].events[0].kind == Kind::instant);
    CHECK(trace.threads[0].events[1].kind == Kind::frame);
    const glz::generic root = parse_json(to_chrome_json(trace)); // valid JSON over the one dump
    CHECK(field(root, "traceEvents").size() == 3);               // i(mark), i(frame), M
}

TEST_CASE("a dump with no end chunk still reads, with one warning")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1};
    static constexpr Site mark = make_site("mark");
    r.name_thread("main");
    r.instant(mark);
    auto bytes = dumped(r);

    const auto chunks = walk_chunks(bytes);
    REQUIRE(!chunks.empty());
    REQUIRE(chunks.back().type == Chunk::end);
    bytes.resize(chunks.back().header); // cut the end chunk off

    const Trace trace = read_trace(bytes);
    CHECK(trace.warnings.size() == 1); // a missing end chunk is a warning, not an error
    REQUIRE(trace.threads.size() == 1);
    CHECK(trace.threads[0].events.size() == 1);
    CHECK(trace.threads[0].events[0].kind == Kind::instant);
    parse_json(to_chrome_json(trace)); // still valid JSON
}

TEST_CASE("a ring that wrapped mid-block: valid JSON, the orphan E dropped, the open B closed at the last timestamp")
{
    std::vector<Event> storage(8);
    Recorder r{storage, 1}; // one ring of 8 events
    static constexpr Site block_a = make_site("blockA");
    static constexpr Site tick = make_site("tick");
    static constexpr Site block_b = make_site("blockB");
    r.name_thread("main");
    r.begin(block_a);                       // overwritten once the ring wraps: an orphan end below
    for (int i = 0; i < 10; ++i)
        r.instant(tick);
    r.end(block_a);                         // its begin is gone: orphan
    r.begin(block_b);                       // never ended
    const auto bytes = dumped(r);

    const Trace trace = read_trace(bytes);
    REQUIRE(trace.threads.size() == 1);
    const Trace::Thread& t = trace.threads[0];
    CHECK(t.lost == 5); // 13 marks into a ring of 8
    REQUIRE(t.events.size() == 8);
    for (std::size_t i = 0; i < 6; ++i)
        CHECK(t.events[i].kind == Kind::instant);
    CHECK(t.events[6].kind == Kind::end);   // the orphan end: read raw...
    CHECK(t.events[6].site == ptr_of(block_a));
    CHECK(t.events[7].kind == Kind::begin); // ...and the open begin
    CHECK(t.events[7].site == ptr_of(block_b));

    const glz::generic root = parse_json(to_chrome_json(trace)); // valid JSON despite the wrap
    const auto b = events_with(root, "B");
    const auto e = events_with(root, "E");
    CHECK(b.size() == 1);
    CHECK(e.size() == 1); // the orphan E is dropped: no E without its B
    CHECK(str_field(*b[0], "name") == "blockB");
    CHECK(str_field(*e[0], "name") == "blockB");
    CHECK(events_with(root, "E", "blockA").empty());
    // The open B is closed at the thread's last timestamp.
    const double last_ts = ts_us(trace, t.events.back().ticks);
    CHECK(num_field(*b[0], "ts") == doctest::Approx(last_ts));
    CHECK(num_field(*e[0], "ts") == doctest::Approx(last_ts));
    CHECK(events_with(root, "i").size() == 6);
}

TEST_CASE("a hand-built dump with pointer size 4 reads correctly (site pointers are raw bits, never reinterpreted)")
{
    std::vector<std::byte> d;
    d.insert(d.end(), prism::profile::sync_marker.begin(), prism::profile::sync_marker.end());
    { // header: version u16 | pointer size u8 | ticks per second u64 | thread count u64 | dropped u64
        std::vector<std::byte> p;
        put_u16(p, prism::profile::format_version);
        put_u8(p, 4);          // the device's pointer size: 32-bit
        put_u64(p, 1'000'000); // ticks per second
        put_u64(p, 1);         // one thread
        put_u64(p, 0);         // none dropped
        append_chunk(d, Chunk::header, p);
    }
    { // thread: id u64 | lost u64 | name length u8 | name bytes
        std::vector<std::byte> p;
        put_u64(p, 7);
        put_u64(p, 0);
        put_u8(p, 4);
        put_bytes(p, "main");
        append_chunk(d, Chunk::thread, p);
    }
    const std::uint64_t site_ptr = 0x12345678; // a 32-bit device's pointer, carried as u64 bits
    { // sites: pointer u64 | line u32 | name, function, file (each u16 length + bytes)
        std::vector<std::byte> p;
        put_u64(p, site_ptr);
        put_u32(p, 42);
        put_string16(p, "mark");
        put_string16(p, "fn");
        put_string16(p, "f.cpp");
        append_chunk(d, Chunk::sites, p);
    }
    { // events: thread id u64, then per event ticks u64 | site u64 | value i32 | kind u8
        std::vector<std::byte> p;
        put_u64(p, 7);
        const auto ev = [&p](std::uint64_t ticks, std::int32_t value, Kind kind) {
            put_u64(p, ticks);
            put_u64(p, site_ptr);
            put_u32(p, static_cast<std::uint32_t>(value)); // i32: its two's-complement bits
            put_u8(p, static_cast<std::uint8_t>(kind));
        };
        ev(1000, 0, Kind::begin);
        ev(2000, 0, Kind::end);
        ev(3000, 0, Kind::instant);
        ev(4000, -7, Kind::counter);
        ev(5000, 0, Kind::frame);
        append_chunk(d, Chunk::events, p);
    }
    append_chunk(d, Chunk::end, std::span<const std::byte>{});

    const Trace trace = read_trace(std::span<const std::byte>{d.data(), d.size()});
    CHECK(trace.warnings.empty());
    CHECK(trace.ticks_per_second == 1'000'000);
    CHECK(trace.threads_dropped == 0);
    REQUIRE(trace.threads.size() == 1);
    CHECK(trace.threads[0].id == 7);
    CHECK(trace.threads[0].name == "main");
    CHECK(trace.threads[0].lost == 0);
    REQUIRE(trace.threads[0].events.size() == 5);
    CHECK(trace.threads[0].events[0].kind == Kind::begin);
    CHECK(trace.threads[0].events[1].kind == Kind::end);
    CHECK(trace.threads[0].events[2].kind == Kind::instant);
    CHECK(trace.threads[0].events[3].kind == Kind::counter);
    CHECK(trace.threads[0].events[3].value == -7);
    CHECK(trace.threads[0].events[4].kind == Kind::frame);
    for (const auto& ev : trace.threads[0].events)
        CHECK(ev.site == site_ptr); // raw bits, not a host pointer
    REQUIRE(trace.sites.size() == 1);
    const SiteInfo& s = trace.sites.at(site_ptr);
    CHECK(s.name == "mark");
    CHECK(s.function == "fn");
    CHECK(s.file == "f.cpp");
    CHECK(s.line == 42);

    const glz::generic root = parse_json(to_chrome_json(trace));
    CHECK(events_with(root, "B").size() == 1);
    CHECK(events_with(root, "E").size() == 1);
    CHECK(events_with(root, "i").size() == 2); // the instant and the frame
    CHECK(events_with(root, "i", "frame").size() == 1);
    CHECK(events_with(root, "C").size() == 1);
    CHECK(events_with(root, "M").size() == 1);
    const glz::generic* b = events_with(root, "B")[0];
    CHECK(str_field(*b, "name") == "mark");
    CHECK(num_field(*b, "tid") == 7);
    CHECK(num_field(*b, "pid") == 1);
    CHECK(num_field(*b, "ts") == doctest::Approx(1000.0)); // 1000 ticks at 1 MHz = 1000 us
    CHECK(num_field(field(*b, "args"), "line") == 42);
    CHECK(str_field(field(*b, "args"), "file") == "f.cpp");
    const glz::generic* c = events_with(root, "C")[0];
    CHECK(str_field(*c, "name") == "mark");
    CHECK(num_field(field(*c, "args"), "value") == -7);
}
