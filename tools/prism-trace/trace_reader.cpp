// The reader half of prism-trace: walks the framed chunks of trace_format.hpp from the first
// sync marker to the dump's end chunk -- one function per chunk type. Every integer is read
// little-endian byte by byte, so the host's endianness does not matter, and the device's pointer
// size is irrelevant: site pointers travel as raw u64 bits and are never reinterpreted as host
// pointers (a 32-bit device's 0x12345678 reads fine on a 64-bit host).
#include "trace_reader.hpp"

#include <bit>
#include <concepts>
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

// header: version u16 | pointer size u8 | ticks per second u64 | thread count u64 |
//         threads dropped u64
void read_header(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    const std::uint16_t version = r.u<std::uint16_t>();
    r.u<std::uint8_t>(); // the device's pointer size: informational only, never reinterpreted
    const std::uint64_t ticks_per_second = r.u<std::uint64_t>();
    r.u<std::uint64_t>(); // the thread count: the thread chunks carry the threads
    const std::uint64_t threads_dropped = r.u<std::uint64_t>();
    if (!r.ok()) {
        trace.warnings.push_back("malformed header chunk, skipped");
        return;
    }
    if (version != prism::profile::format_version)
        trace.warnings.push_back("dump format version " + std::to_string(version) + ", expected " +
                                 std::to_string(prism::profile::format_version));
    trace.ticks_per_second = static_cast<double>(ticks_per_second);
    trace.threads_dropped = threads_dropped;
}

// thread: id u64 | lost u64 | name length u8 | name bytes
void read_thread(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    Trace::Thread thread;
    thread.id = r.u<std::uint64_t>();
    thread.lost = r.u<std::uint64_t>();
    thread.name = std::string(r.str8());
    if (!r.ok()) {
        trace.warnings.push_back("malformed thread chunk, skipped");
        return;
    }
    trace.threads.push_back(std::move(thread));
}

// sites: per distinct site pointer: pointer u64 | line u32 | name, function, file (u16 length +
// bytes each)
void read_sites(Trace& trace, std::span<const std::byte> payload)
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
            return;
        }
        trace.sites[pointer] = std::move(info); // the sites chunks repeat across events chunks: merge by pointer
    }
}

// events: thread id u64, then per event ticks u64 | site pointer u64 | value i32 | kind u8
void read_events(Trace& trace, std::span<const std::byte> payload)
{
    PayloadReader r{payload};
    const std::uint64_t thread_id = r.u<std::uint64_t>();
    if (!r.ok()) {
        trace.warnings.push_back("malformed events chunk, skipped");
        return;
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
    for (std::size_t i = 0; i < count; ++i) {
        TraceEvent event;
        event.ticks = r.u<std::uint64_t>();
        event.site = r.u<std::uint64_t>(); // the device-side pointer's bits, never reinterpreted
        event.value = std::bit_cast<std::int32_t>(r.u<std::uint32_t>()); // i32: its two's-complement bits
        event.kind = static_cast<Kind>(r.u<std::uint8_t>());
        if (!r.ok()) {
            trace.warnings.push_back("malformed events chunk, skipped");
            return;
        }
        thread->events.push_back(event); // oldest first: the chunks come in order
    }
}

// One CRC-valid chunk. Returns true when it is the dump's end chunk.
bool read_chunk(Trace& trace, Chunk type, std::span<const std::byte> payload)
{
    switch (type) {
    case Chunk::header:
        read_header(trace, payload);
        return false;
    case Chunk::thread:
        read_thread(trace, payload);
        return false;
    case Chunk::sites:
        read_sites(trace, payload);
        return false;
    case Chunk::events:
        read_events(trace, payload);
        return false;
    case Chunk::end:
        return true;
    }
    trace.warnings.push_back("unknown chunk type " +
                             std::to_string(static_cast<unsigned>(static_cast<std::uint8_t>(type))) +
                             ", skipped");
    return false;
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
    pos += prism::profile::sync_marker.size();

    bool ended = false;
    while (!ended && pos < bytes.size()) {
        if (pos + 5 > bytes.size())
            break; // a truncated chunk header: the dump ends here
        const auto type = static_cast<Chunk>(static_cast<std::uint8_t>(bytes[pos]));
        const std::uint32_t length = load_u32(bytes, pos + 1);
        const std::uint64_t chunk_end = static_cast<std::uint64_t>(pos) + 5 + length + 4;
        if (chunk_end > bytes.size()) {
            // The length cannot be trusted (a damaged chunk): resynchronise on the next marker.
            trace.warnings.push_back("damaged chunk at offset " + std::to_string(pos) +
                                     ": its length runs past the end, skipped");
            pos = find_sync(bytes, pos + 1);
            continue;
        }
        const std::span<const std::byte> payload = bytes.subspan(pos + 5, length);
        if (prism::profile::crc32(bytes.subspan(pos, 5 + static_cast<std::size_t>(length))) !=
            load_u32(bytes, pos + 5 + length)) {
            trace.warnings.push_back("chunk at offset " + std::to_string(pos) + ": CRC mismatch, skipped");
            pos = static_cast<std::size_t>(chunk_end); // the length is plausible: skip by it
            continue;
        }
        pos = static_cast<std::size_t>(chunk_end);
        ended = read_chunk(trace, type, payload); // bytes after the end chunk are ignored
    }
    if (!ended)
        trace.warnings.push_back("no end chunk: the dump is truncated");
    return trace;
}

} // namespace prism_trace
