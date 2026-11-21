// The prism-trace host tool (tools/prism-trace): read_trace over real dumps -- Recorder +
// make_site + prism::profile::dump into io::MemoryStream -- and to_chrome_json, checked through
// Glaze's generic JSON read (glz::generic; this Glaze, 9.1.0, has glz::generic, with json_t a
// deprecated alias of it). Round trip (two threads, a block, an instant, a counter, a frame);
// a byte flipped inside an events chunk (that chunk skipped, one warning, the other threads
// still read); garbage bytes before the sync marker and after the end chunk (ignored); a dump
// with no end chunk (a warning, not an error); a ring that wrapped mid-block (valid JSON, the
// orphan E dropped, the open B closed at the thread's last timestamp); a hand-built dump with
// pointer size 4 (site pointers are raw u64 bits, never reinterpreted).
//
// Fix round 1, step A (task-3-review.md C1/I2/I3/I4/I5): the damaged-dump cases -- one byte
// removed from an events chunk, one inserted, one bit flipped in its length field (each: that
// chunk skipped with one warning, the next thread's events and the end chunk still read); a
// dump truncated mid-chunk; a CRC-valid sites chunk whose string length runs past its payload;
// an unknown chunk type with a valid CRC; a header with version 2 (a warning and no threads);
// a thread name cut mid-UTF-8 (the JSON still valid UTF-8 and parseable); and the step-B CLI
// predicates pinned here: prism_trace::is_serial_path and Trace::found.
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
using prism_trace::is_serial_path;
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

// The first chunk of `type` in the walk, or nullptr.
const ChunkSpan* find_chunk(const std::vector<ChunkSpan>& chunks, Chunk type)
{
    for (const ChunkSpan& c : chunks)
        if (c.type == type)
            return &c;
    return nullptr;
}

// --- damaging a dump ---------------------------------------------------------------------------

// A UART overrun drops one byte: everything after `at` shifts left by one.
void remove_byte_at(std::vector<std::byte>& v, std::size_t at)
{
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(at));
}

// A duplicated byte on the line: everything from `at` shifts right by one.
void insert_byte_at(std::vector<std::byte>& v, std::size_t at, std::byte b)
{
    v.insert(v.begin() + static_cast<std::ptrdiff_t>(at), b);
}

// One bit of the byte at `at` flips (a line error in a chunk's length field).
void flip_bit_at(std::vector<std::byte>& v, std::size_t at, unsigned bit)
{
    v[at] = static_cast<std::byte>(static_cast<unsigned int>(v[at]) ^ (1u << bit));
}

// Recomputes the CRC of the chunk at `c` after an in-place payload edit, so a hand-damaged
// chunk still passes the CRC check (the damage is in the payload's meaning, not its framing).
void fix_chunk_crc(std::vector<std::byte>& bytes, const ChunkSpan& c)
{
    const std::uint32_t crc =
        prism::profile::crc32(std::span<const std::byte>{bytes.data() + c.header, 5 + c.length});
    for (std::size_t i = 0; i < 4; ++i)
        bytes[c.header + 5 + c.length + i] = static_cast<std::byte>((crc >> (8 * i)) & 0xFFu);
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

// The two-thread dump the damage tests start from: main (slot 0, two instants at site "a"),
// then worker (slot 1, two instants at site "b"). The dump writes chunks in slot order, so the
// first events chunk is main's, the last events chunk the worker's, and the last chunk the end.
std::vector<std::byte> two_thread_dump()
{
    static constexpr Site a = make_site("a");
    static constexpr Site b = make_site("b");
    std::vector<Event> storage(256);
    Recorder r{storage, 2}; // 128 events per ring: no wrap
    r.name_thread("main");
    r.instant(a);
    r.instant(a);
    std::jthread worker([&] {
        r.name_thread("worker");
        r.instant(b);
        r.instant(b);
    });
    worker.join();
    return dumped(r);
}

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

// True when one of the trace's warnings contains `needle`.
bool has_warning(const Trace& trace, std::string_view needle)
{
    for (const std::string& w : trace.warnings)
        if (w.find(needle) != std::string::npos)
            return true;
    return false;
}

// What the C1 tests expect once the first events chunk is damaged: exactly one warning (the
// damaged chunk -- the end chunk was still read), both threads and both sites known, the
// damaged thread's events gone, the other thread's two intact, valid JSON over what survived.
void check_first_events_chunk_skipped(const Trace& trace)
{
    CHECK(trace.warnings.size() == 1);
    CHECK(!has_warning(trace, "no end chunk"));
    REQUIRE(trace.threads.size() == 2);
    CHECK(trace.sites.size() == 2);
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
    const glz::generic root = parse_json(to_chrome_json(trace));
    CHECK(events_with(root, "i").size() == 2);
}

// A small UTF-8 validity check (RFC 3629): no stray continuation bytes, no truncated
// sequences, no overlongs, no surrogates, nothing past U+10FFFF.
bool is_valid_utf8(std::string_view s)
{
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        if (b0 < 0x80) {
            ++i;
            continue;
        }
        std::size_t len = 0;
        unsigned char lo = 0x80, hi = 0xBF; // the first continuation byte's allowed range
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            len = 1;
        } else if (b0 == 0xE0) {
            len = 2;
            lo = 0xA0; // no overlong 3-byte forms
        } else if (b0 >= 0xE1 && b0 <= 0xEC) {
            len = 2;
        } else if (b0 == 0xED) {
            len = 2;
            hi = 0x9F; // no surrogates
        } else if (b0 >= 0xEE && b0 <= 0xEF) {
            len = 2;
        } else if (b0 == 0xF0) {
            len = 3;
            lo = 0x90; // no overlong 4-byte forms
        } else if (b0 >= 0xF1 && b0 <= 0xF3) {
            len = 3;
        } else if (b0 == 0xF4) {
            len = 3;
            hi = 0x8F; // nothing past U+10FFFF
        } else {
            return false; // 0x80-0xC1, 0xF5-0xFF: a stray continuation or an invalid lead byte
        }
        if (i + len >= s.size())
            return false; // the sequence is cut at the end
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        if (b1 < lo || b1 > hi)
            return false;
        for (std::size_t k = 2; k <= len; ++k) {
            const auto bk = static_cast<unsigned char>(s[i + k]);
            if (bk < 0x80 || bk > 0xBF)
                return false;
        }
        i += len + 1;
    }
    return true;
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

    // 10 bytes of garbage before the sync marker, 7 after the end chunk. Sized up front: GCC 14
    // warns (stringop-overflow, a false positive) on growing the vector with insert().
    std::vector<std::byte> noisy(10 + bytes.size() + 7, std::byte{0x00});
    std::fill_n(noisy.begin(), 10, std::byte{0xAB});
    std::ranges::copy(bytes, noisy.begin() + 10);

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

TEST_CASE("one byte removed inside an events chunk: that chunk is skipped, the next thread's events and the end chunk still read (C1)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* events = find_chunk(chunks, Chunk::events); // the first one: main's
    REQUIRE(events != nullptr);
    remove_byte_at(bytes, events->payload + events->length / 2); // a dropped byte mid-payload
    check_first_events_chunk_skipped(read_trace(bytes));
}

TEST_CASE("one byte inserted inside an events chunk: same expectations (C1)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* events = find_chunk(chunks, Chunk::events);
    REQUIRE(events != nullptr);
    insert_byte_at(bytes, events->payload + events->length / 2, std::byte{0x00}); // a duplicated byte
    check_first_events_chunk_skipped(read_trace(bytes));
}

TEST_CASE("one bit flipped in an events chunk's length field: same expectations (C1)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* events = find_chunk(chunks, Chunk::events);
    REQUIRE(events != nullptr);
    flip_bit_at(bytes, events->header + 4, 7); // the length u32's top bit: now about 2^31
    check_first_events_chunk_skipped(read_trace(bytes));
}

TEST_CASE("a dump truncated in the middle of an events chunk: no crash, a warning, the earlier chunks read (I5)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    REQUIRE(chunks.size() >= 2);
    REQUIRE(chunks.back().type == Chunk::end);
    const ChunkSpan& last_events = chunks[chunks.size() - 2]; // the worker's events chunk
    REQUIRE(last_events.type == Chunk::events);
    bytes.resize(last_events.payload + last_events.length / 2); // cut mid-payload

    const Trace trace = read_trace(bytes); // must not crash
    CHECK(!trace.warnings.empty());      // the truncated chunk is reported
    REQUIRE(trace.threads.size() == 2);  // both thread chunks were read before the cut
    const Trace::Thread* main_t = nullptr;
    for (const Trace::Thread& t : trace.threads)
        if (t.name == "main")
            main_t = &t;
    REQUIRE(main_t != nullptr);
    CHECK(main_t->events.size() == 2); // the earlier events chunk was read whole
    parse_json(to_chrome_json(trace)); // still valid JSON
}

TEST_CASE("a CRC-valid sites chunk whose string length runs past its payload: skipped with a warning, no crash (I5)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* sites = find_chunk(chunks, Chunk::sites); // the first one: main's
    REQUIRE(sites != nullptr);
    // The first site entry is pointer u64 | line u32 | name u16 length: claim a 65535-byte name.
    bytes[sites->payload + 12] = std::byte{0xFF};
    bytes[sites->payload + 13] = std::byte{0xFF};
    fix_chunk_crc(bytes, *sites); // the damage is in the payload's meaning: the CRC still passes

    const Trace trace = read_trace(bytes);
    CHECK(trace.warnings.size() == 1);
    CHECK(has_warning(trace, "malformed sites chunk"));
    CHECK(!has_warning(trace, "no end chunk")); // the end chunk was still read
    REQUIRE(trace.threads.size() == 2);
    CHECK(trace.sites.size() == 1); // the worker's sites chunk survived; the damaged one did not
    std::size_t total = 0;
    for (const Trace::Thread& t : trace.threads)
        total += t.events.size();
    CHECK(total == 4); // the events chunks are separate: all four events still read
    parse_json(to_chrome_json(trace));
}

TEST_CASE("an unknown chunk type with a valid CRC: skipped with a warning, the rest read (I5)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    REQUIRE(!chunks.empty());
    REQUIRE(chunks.back().type == Chunk::end);
    std::vector<std::byte> extra;
    std::vector<std::byte> payload{std::byte{'x'}, std::byte{'y'}, std::byte{'z'}};
    append_chunk(extra, static_cast<Chunk>(0x7F), payload); // a type the reader does not know
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(chunks.back().header), extra.begin(),
                 extra.end()); // just before the end chunk

    const Trace trace = read_trace(bytes);
    CHECK(trace.warnings.size() == 1);
    CHECK(has_warning(trace, "unknown chunk type"));
    CHECK(!has_warning(trace, "no end chunk"));
    REQUIRE(trace.threads.size() == 2);
    CHECK(trace.sites.size() == 2);
    std::size_t total = 0;
    for (const Trace::Thread& t : trace.threads)
        total += t.events.size();
    CHECK(total == 4);
    parse_json(to_chrome_json(trace));
}

TEST_CASE("a header chunk with version 2 (CRC recomputed): a warning and no threads (I5/M3 -- our call: an unknown version is not read)")
{
    auto bytes = two_thread_dump();
    const auto chunks = walk_chunks(bytes);
    const ChunkSpan* header = find_chunk(chunks, Chunk::header);
    REQUIRE(header != nullptr);
    bytes[header->payload] = std::byte{2}; // version u16 LE = 2
    bytes[header->payload + 1] = std::byte{0};
    fix_chunk_crc(bytes, *header);

    const Trace trace = read_trace(bytes);
    // Our call, pinned here: a format version we do not know is a warning and the dump is not
    // read -- no threads -- rather than mis-parsed as version 1 (M3).
    CHECK(!trace.warnings.empty());
    CHECK(has_warning(trace, "version"));
    CHECK(trace.threads.empty());
}

TEST_CASE("a thread name cut mid-UTF-8 sequence: the JSON is valid UTF-8 and still parses (I2)")
{
    std::vector<Event> storage(64);
    Recorder r{storage, 1};
    static constexpr Site mark = make_site("mark");
    // 15 ASCII chars + é (0xC3 0xA9): name_thread keeps 16 bytes, so the stored name ends on
    // the lone lead byte 0xC3 -- not valid UTF-8.
    r.name_thread("abcdefghijklmno\xC3\xA9");
    r.instant(mark);
    const auto bytes = dumped(r);

    const Trace trace = read_trace(bytes);
    REQUIRE(trace.threads.size() == 1);
    CHECK(trace.threads[0].name.size() == 16); // the cut really is mid-character

    const std::string json = to_chrome_json(trace);
    CHECK(is_valid_utf8(json));          // the writer must not pass the invalid byte through raw
    const glz::generic root = parse_json(json); // and Glaze still reads the output
    const auto m = events_with(root, "M");
    REQUIRE(m.size() == 1);
    CHECK(!str_field(field(*m[0], "args"), "name").empty());
}

TEST_CASE("the CLI predicates pinned for step B: is_serial_path, and read_trace's found (I3/I4)")
{
    // Serial devices: a /dev/ path, or COM followed only by digits.
    CHECK(is_serial_path("/dev/ttyACM0"));
    CHECK(is_serial_path("COM3"));
    CHECK(is_serial_path("COM12"));
    // Files whose name merely starts with "com" (or COM with no digits) are files.
    CHECK(!is_serial_path("com_dump.bin"));
    CHECK(!is_serial_path("combined.prtr"));
    CHECK(!is_serial_path("COM"));
    CHECK(!is_serial_path("COMX"));

    // Bytes with no sync marker are not a dump.
    const std::vector<std::byte> garbage(64, std::byte{0xAB});
    const Trace none = read_trace(garbage);
    CHECK(none.found == false);
    CHECK(!none.warnings.empty());
    CHECK(none.threads.empty());

    // A real dump is found (red until read_trace sets found in step B).
    const std::vector<std::byte> dump = two_thread_dump();
    const Trace real = read_trace(dump);
    CHECK(real.found == true);
}
