#pragma once

// The framed binary dump that dump() (dump.hpp) writes and the host tool prism-trace reads
// (doc/design/profiling.md) -- one shared header, so the two cannot drift. A dump is the sync
// marker followed by chunks; a chunk is
//     type (u8) | payload length (u32 LE) | payload | CRC-32 (u32 LE) of type+length+payload
// so a host can find a dump inside a byte stream (a UART has no flow control) and reject a
// damaged chunk instead of mis-reading it. All integers are little-endian whatever the
// target's endianness. Payloads:
//     header: version u16 | pointer size u8 | ticks per second u64 | thread count u64 |
//             threads dropped u64
//     thread: id u64 | lost u64 | name length u8 | name bytes
//     sites:  per distinct site pointer: pointer u64 | line u32 | name (u16 length + bytes) |
//             function (u16 length + bytes) | file (u16 length + bytes) -- each string the
//             strlen of the site's, cut at 65535
//     events: thread id u64, then per event (oldest first, at most max_events_per_chunk in
//             one chunk): ticks u64 | site pointer u64 | value i32 | kind u8 --
//             event_wire_size bytes each
//     end:    empty
// Chunk order: header, one thread chunk per thread, then for each thread one or more
// (sites, events) pairs -- the sites chunk before an events chunk carries the distinct site
// pointers that events chunk uses -- then end.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::profile {

// An event's kind, as recorded and as written in an events chunk (one byte).
enum class Kind : std::uint8_t { begin, end, instant, counter, frame };

inline constexpr std::array<std::byte, 4> sync_marker{std::byte{'P'}, std::byte{'R'}, std::byte{'T'}, std::byte{'R'}};
inline constexpr std::uint16_t format_version = 1;

enum class Chunk : std::uint8_t { header, thread, sites, events, end };

inline constexpr std::size_t max_events_per_chunk = 4096; // events in one events chunk
inline constexpr std::size_t max_sites_per_chunk = 256;   // the dump's fixed dedup table
inline constexpr std::size_t event_wire_size = 8 + 8 + 4 + 1; // ticks, site pointer, value, kind

// The running CRC-32 (IEEE 802.3: reflected, polynomial 0xEDB88320): start at 0xFFFFFFFF, feed
// every span, final XOR 0xFFFFFFFF -- so the CRC of no bytes is 0. The 256-entry table is
// generated at compile time.
constexpr std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::byte> bytes)
{
    static constexpr std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit)
                c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    for (const std::byte b : bytes)
        crc = table[(crc ^ static_cast<std::uint32_t>(b)) & 0xFFu] ^ (crc >> 8);
    return crc;
}

// The CRC-32 of the bytes: the value a chunk stores after its payload (over type+length+
// payload). Known answer: crc32("123456789") == 0xCBF43926.
constexpr std::uint32_t crc32(std::span<const std::byte> bytes)
{
    return crc32_update(0xFFFFFFFFu, bytes) ^ 0xFFFFFFFFu;
}

} // namespace prism::profile
