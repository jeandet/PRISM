// The reader half of prism-trace: walks the framed chunks of trace_format.hpp from the first
// sync marker to the dump's end chunk -- one function per chunk type. Every integer is read
// little-endian byte by byte, so the host's endianness does not matter, and the device's pointer
// size is irrelevant: site pointers travel as raw u64 bits and are never reinterpreted as host
// pointers (a 32-bit device's 0x12345678 reads fine on a 64-bit host). A damaged chunk is not
// trusted -- not even its length field: one warning per skipped region, then find_next_chunk
// scans forward one byte at a time for the next complete valid chunk and resumes there.
#include "trace_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace prism_trace {
namespace {

using prism::profile::Chunk;
using prism::profile::Kind;

// One little-endian cursor over a chunk's payload. ok() goes false once a read runs past the
// end, so a malformed (but CRC-valid) chunk is rejected instead of read out of bounds.
class PayloadReader {
public:
    explicit PayloadReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool ok() const { return ok_; }
    std::size_t left() const { return bytes_.size() - pos_; }

    template <std::unsigned_integral UInt>
    UInt u()
    {
        if (!ok_ || left() < sizeof(UInt)) {
            ok_ = false;
            return UInt{};
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < sizeof(UInt); ++i)
            value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes_[pos_ + i])) << (8 * i);
        pos_ += sizeof(UInt);
        return static_cast<UInt>(value);
    }

    // The bytes of a length-prefixed string whose prefix the caller has already read.
    std::string_view bytes(std::size_t length)
    {
        if (!ok_ || left() < length) {
            ok_ = false;
            return {};
        }
        const std::string_view s{reinterpret_cast<const char*>(bytes_.data() + pos_), length};
        pos_ += length;
        return s;
    }

    std::string_view str8() { return bytes(u<std::uint8_t>()); }
    std::string_view str16() { return bytes(u<std::uint16_t>()); }

private:
    std::span<const std::byte> bytes_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

// The offset of the sync marker at or after `from`, or bytes.size() when there is none.
std::size_t find_sync(std::span<const std::byte> bytes, std::size_t from)
{
    constexpr auto marker = prism::profile::sync_marker;
    for (std::size_t i = from; i + marker.size() <= bytes.size(); ++i) {
        bool match = true;
        for (std::size_t k = 0; k < marker.size(); ++k)
            if (bytes[i + k] != marker[k]) {
                match = false;
                break;
            }
        if (match)
            return i;
    }
    return bytes.size();
}

std::uint32_t load_u32(std::span<const std::byte> bytes, std::size_t at)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + i])) << (8 * i);
    return value;
}

Trace::Thread* find_thread(Trace& trace, std::uint64_t id)
{
    for (Trace::Thread& thread : trace.threads)
        if (thread.id == id)
            return &thread;
    return nullptr;
}

// What reading one chunk did. `damaged` means its payload did not parse (a warning says so) and
// the caller resynchronises; `refuse` means the dump's format version is unknown, so the dump is
// not read at all rather than mis-parsed.
enum class ChunkRead { ok, end, damaged, refuse };

// A complete, CRC-valid chunk sitting at `header` (valid says so). Anything else -- an unknown
// type, a length that runs past the input, a CRC mismatch, a truncated header -- is not a chunk.
struct ChunkAt {
    std::size_t header = 0; // offset of the type byte
    Chunk type = Chunk::header;
    std::uint32_t length = 0;
    std::size_t end = 0; // one past the chunk's CRC
    bool valid = false;
};

ChunkAt chunk_at(std::span<const std::byte> bytes, std::size_t header)
{
    ChunkAt c;
    c.header = header;
    if (header + 5 > bytes.size())
        return c; // a truncated chunk header
    const auto type_byte = static_cast<std::uint8_t>(bytes[header]);
    if (type_byte > static_cast<std::uint8_t>(Chunk::end))
        return c; // not a known chunk type
    c.type = static_cast<Chunk>(type_byte);
    c.length = load_u32(bytes, header + 1);
    const std::uint64_t end = static_cast<std::uint64_t>(header) + 5 + c.length + 4;
    if (end > bytes.size())
        return c; // the length runs past the input
    const std::size_t body = 5 + static_cast<std::size_t>(c.length); // fits: end <= size was checked
    if (prism::profile::crc32(bytes.subspan(header, body)) != load_u32(bytes, header + body))
        return c; // CRC mismatch
    c.end = static_cast<std::size_t>(end);
    c.valid = true;
    return c;
}

// Why chunk_at rejected the offset `at`: the skip warning's text.
std::string damage_at(std::span<const std::byte> bytes, std::size_t at)
{
    if (at + 5 > bytes.size())
        return "truncated chunk header at offset " + std::to_string(at);
    const auto type_byte = static_cast<std::uint8_t>(bytes[at]);
    if (type_byte > static_cast<std::uint8_t>(Chunk::end))
        return "unknown chunk type " + std::to_string(type_byte) + " at offset " +
               std::to_string(at);
    if (static_cast<std::uint64_t>(at) + 5 + load_u32(bytes, at + 1) + 4 > bytes.size())
        return "chunk at offset " + std::to_string(at) + ": its length runs past the end";
    return "chunk at offset " + std::to_string(at) + ": CRC mismatch";
}

// The next offset at or after `from` where a complete valid chunk sits (chunk_at accepts it).
// A sync marker found while scanning is not a chunk: it is skipped past. bytes.size() when there
// is none.
// simplify: the scan costs a CRC per candidate offset -- O(n * chunk) in the worst case, fine on
// a host; a random offset passes the type and length checks with probability about 2^-24 and the
// CRC with 2^-32, so a false hit does not happen on a real dump.
std::size_t find_next_chunk(std::span<const std::byte> bytes, std::size_t from)
{
    constexpr auto marker = prism::profile::sync_marker;
    for (std::size_t i = from; i < bytes.size(); ++i) {
        bool is_marker = i + marker.size() <= bytes.size();
        if (is_marker)
            for (std::size_t k = 0; k < marker.size(); ++k)
                if (bytes[i + k] != marker[k]) {
                    is_marker = false;
                    break;
                }
        if (is_marker) {
            i += marker.size() - 1; // a marker is not a chunk: skip past it
            continue;
        }
        if (chunk_at(bytes, i).valid)
            return i;
    }
    return bytes.size();
}

// header: version u16 | pointer size u8 | ticks per second u64 | thread count u64 |
//         threads dropped u64
ChunkRead read_header(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    const std::uint16_t version = r.u<std::uint16_t>();
    r.u<std::uint8_t>(); // the device's pointer size: informational only, never reinterpreted
    const std::uint64_t ticks_per_second = r.u<std::uint64_t>();
    r.u<std::uint64_t>(); // the thread count: the thread chunks carry the threads
    const std::uint64_t threads_dropped = r.u<std::uint64_t>();
    if (!r.ok()) {
        trace.warnings.push_back("malformed header chunk, skipped");
        return ChunkRead::damaged;
    }
    if (version != prism::profile::format_version) {
        // An unknown format version is not read as version 1: a future format's payloads would
        // be mis-parsed into a plausible but wrong trace.
        trace.warnings.push_back("dump format version " + std::to_string(version) + ", expected " +
                                 std::to_string(prism::profile::format_version) +
                                 ": the dump is not read");
        return ChunkRead::refuse;
    }
    trace.ticks_per_second = static_cast<double>(ticks_per_second);
    trace.threads_dropped = threads_dropped;
    return ChunkRead::ok;
}

// thread: id u64 | lost u64 | name length u8 | name bytes
ChunkRead read_thread(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    Trace::Thread thread;
    thread.id = r.u<std::uint64_t>();
    thread.lost = r.u<std::uint64_t>();
    thread.name = std::string(r.str8());
    if (!r.ok()) {
        trace.warnings.push_back("malformed thread chunk, skipped");
        return ChunkRead::damaged;
    }
    trace.threads.push_back(std::move(thread));
    return ChunkRead::ok;
}

// sites: per distinct site pointer: pointer u64 | line u32 | name, function, file (u16 length +
// bytes each)
ChunkRead read_sites(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    while (r.ok() && r.left() > 0) {
        const std::uint64_t pointer = r.u<std::uint64_t>();
        SiteInfo info;
        info.line = r.u<std::uint32_t>();
        info.name = std::string(r.str16());
        info.function = std::string(r.str16());
        info.file = std::string(r.str16());
        if (!r.ok()) {
            trace.warnings.push_back("malformed sites chunk, skipped");
            return ChunkRead::damaged;
        }
        trace.sites[pointer] = std::move(info); // the sites chunks repeat across events chunks: merge by pointer
    }
    return ChunkRead::ok;
}

// events: thread id u64, then per event ticks u64 | site pointer u64 | value i32 | kind u8
ChunkRead read_events(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    const std::uint64_t thread_id = r.u<std::uint64_t>();
    if (!r.ok()) {
        trace.warnings.push_back("malformed events chunk, skipped");
        return ChunkRead::damaged;
    }
    Trace::Thread* thread = find_thread(trace, thread_id);
    if (thread == nullptr) { // an events chunk whose thread chunk was lost
        trace.threads.push_back(Trace::Thread{});
        trace.threads.back().id = thread_id;
        thread = &trace.threads.back();
    }
    if (r.left() % prism::profile::event_wire_size != 0)
        trace.warnings.push_back("events chunk with a partial event at its end");
    const std::size_t count = r.left() / prism::profile::event_wire_size;
    bool unknown_kind = false;
    for (std::size_t i = 0; i < count; ++i) {
        TraceEvent event;
        event.ticks = r.u<std::uint64_t>();
        event.site = r.u<std::uint64_t>(); // the device-side pointer's bits, never reinterpreted
        event.value = std::bit_cast<std::int32_t>(r.u<std::uint32_t>()); // i32: its two's-complement bits
        const std::uint8_t kind_byte = r.u<std::uint8_t>();
        if (kind_byte > static_cast<std::uint8_t>(Kind::frame))
            unknown_kind = true; // the JSON writer drops it; the warning says why
        event.kind = static_cast<Kind>(kind_byte);
        if (!r.ok()) {
            trace.warnings.push_back("malformed events chunk, skipped");
            return ChunkRead::damaged;
        }
        thread->events.push_back(event); // oldest first: the chunks come in order
    }
    if (unknown_kind)
        trace.warnings.push_back("events chunk with an unknown event kind: those events are dropped");
    return ChunkRead::ok;
}

// Reads one CRC-valid chunk: `end` when it is the dump's end chunk, `damaged` when its payload
// did not parse (a warning says so), `refuse` when the format version is unknown.
ChunkRead read_chunk(Trace& trace, Chunk type, std::span<const std::byte> payload)
{
    switch (type) {
    case Chunk::header:
        return read_header(trace, payload);
    case Chunk::thread:
        return read_thread(trace, payload);
    case Chunk::sites:
        return read_sites(trace, payload);
    case Chunk::events:
        return read_events(trace, payload);
    case Chunk::end:
        return ChunkRead::end;
    }
    return ChunkRead::damaged; // unreachable: chunk_at accepts only known types
}

} // namespace

Trace read_trace(std::span<const std::byte> bytes)
{
    Trace trace;
    std::size_t pos = find_sync(bytes, 0); // anything before the first marker is not the dump
    if (pos == bytes.size()) {
        trace.warnings.push_back("no sync marker: not a profile dump");
        return trace;
    }
    trace.found = true; // a sync marker was found: these bytes hold a dump
    pos += prism::profile::sync_marker.size();

    bool ended = false;   // the end chunk was read: bytes after it are ignored
    bool refused = false; // the format version is unknown: the dump is not read
    while (!ended && !refused && pos < bytes.size()) {
        const ChunkAt chunk = chunk_at(bytes, pos);
        if (!chunk.valid) {
            // A damaged chunk (a dropped, inserted or flipped byte; an unknown type; a length
            // past the end; a CRC mismatch): its length field is NOT trusted -- one warning per
            // skipped region, then resynchronise on the next complete valid chunk.
            trace.warnings.push_back(damage_at(bytes, pos) + ", skipped");
            pos = find_next_chunk(bytes, pos + 1);
            continue;
        }
        const std::span<const std::byte> payload = bytes.subspan(chunk.header + 5, chunk.length);
        switch (read_chunk(trace, chunk.type, payload)) {
        case ChunkRead::ok:
            pos = chunk.end;
            break;
        case ChunkRead::end:
            ended = true; // bytes after the end chunk are ignored
            break;
        case ChunkRead::damaged:
            // A CRC-valid but malformed payload: the chunk failed -- resynchronise from its
            // start + 1, not from its length field.
            pos = find_next_chunk(bytes, chunk.header + 1);
            break;
        case ChunkRead::refuse:
            refused = true;
            break;
        }
    }
    if (!ended && !refused)
        trace.warnings.push_back("no end chunk: the dump is truncated");
    return trace;
}

// A serial device: a path under /dev/, or a COM port name -- "COM" followed only by one or more
// digits, case-insensitive, optionally with the \\.\ prefix Windows needs from COM10 up. A file
// whose name merely starts with "com" (com_dump.bin, combined.prtr) is a file, not a port.
bool is_serial_path(std::string_view path)
{
    if (path.starts_with("/dev/"))
        return true;
    const std::size_t at = path.starts_with("\\\\.\\") ? 4 : 0; // Windows' prefix, from COM10 up
    if (path.size() < at + 4) // "COM" plus at least one digit
        return false;
    const auto upper = [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    };
    if (upper(path[at]) != 'C' || upper(path[at + 1]) != 'O' || upper(path[at + 2]) != 'M')
        return false;
    for (std::size_t i = at + 3; i < path.size(); ++i) // only digits after "COM"
        if (path[i] < '0' || path[i] > '9')
            return false;
    return true;
}

bool dump_complete(std::span<const std::byte> bytes)
{
    using prism::profile::sync_marker;
    const auto sync = std::ranges::search(bytes, sync_marker);
    if (sync.empty())
        return false;
    // The end chunk has an empty payload, so its framing is fixed: type 0x04, length 0, then
    // the CRC-32 of those five bytes, little-endian.
    static constexpr std::array<std::byte, 5> head{std::byte{0x04}, std::byte{0}, std::byte{0},
                                                   std::byte{0}, std::byte{0}};
    static constexpr std::uint32_t crc = prism::profile::crc32(head);
    static constexpr std::array<std::byte, 9> end_chunk{
        head[0], head[1], head[2], head[3], head[4],
        std::byte(crc & 0xFF), std::byte((crc >> 8) & 0xFF), std::byte((crc >> 16) & 0xFF),
        std::byte(crc >> 24)};
    const std::span<const std::byte> after{sync.end(), bytes.end()};
    return !std::ranges::search(after, end_chunk).empty();
}

std::string summary(const Trace& trace)
{
    std::size_t events = 0;
    for (const Trace::Thread& thread : trace.threads)
        events += thread.events.size();
    // ticks_per_second is an exact integer in the double: print it as one.
    return std::to_string(trace.threads.size()) + " threads, " + std::to_string(events) + " events, "
           + std::to_string(trace.warnings.size()) + " warning(s), " + std::to_string(trace.sites.size())
           + " sites, " + std::to_string(static_cast<std::uint64_t>(trace.ticks_per_second))
           + " ticks/s";
}

} // namespace prism_trace
