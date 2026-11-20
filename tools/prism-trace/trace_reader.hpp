#pragma once

// The reader half of prism-trace (doc/design/profiling.md, tools/prism-trace): turns the framed
// binary dump that prism::profile::dump() writes into a Trace. The format itself lives in
// include/prism/profile/trace_format.hpp -- one header shared with the recorder, so the two
// cannot drift; this file only reads it. read_trace reads ONE dump out of the bytes: from the
// first sync marker to the dump's end chunk; bytes after the end chunk are ignored, a missing
// end chunk is a warning (not an error), and a damaged chunk (a bad CRC, an unknown type, a
// length past the input, a malformed payload) is skipped with one warning per skipped region --
// its length field is NOT trusted: the reader scans forward one byte at a time for the next
// complete valid chunk (a known type, a length that fits, a matching CRC) and resumes there.
//
// Since Task 2b events point at call SITES, not names: a sites chunk (pointer u64, line u32,
// then name, function, file as u16 length + bytes each) precedes each events chunk, and the
// sites chunks repeat across events chunks -- they merge by pointer into Trace::sites. The
// device's pointer size may be 4 while the host's is 8, so site pointers travel as raw u64
// bits and are never reinterpreted as host pointers.
#include <prism/profile/trace_format.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prism_trace {

// What a sites chunk says about one call site.
struct SiteInfo {
    std::string name;
    std::string function; // may be empty: the frame site has none
    std::string file;
    std::uint32_t line = 0;
};

// One recorded mark, exactly as the dump carried it: `site` is the device-side pointer's bits.
struct TraceEvent {
    std::uint64_t ticks = 0;
    std::uint64_t site = 0;
    std::int32_t value = 0; // the counter's sample
    prism::profile::Kind kind = prism::profile::Kind::instant;
};

struct Trace {
    // True when a sync marker was found: these bytes hold a dump, not chatter. The CLI keys its
    // exit code on it: not found means an error, exit 1, no output file (I4).
    bool found = false;
    double ticks_per_second = 0;
    struct Thread {
        std::uint64_t id = 0;
        std::string name;
        std::uint64_t lost = 0;            // events the full ring overwrote before the dump
        std::vector<TraceEvent> events;    // oldest first
    };
    std::vector<Thread> threads;
    std::map<std::uint64_t, SiteInfo> sites; // merged across the dump's sites chunks
    std::uint64_t threads_dropped = 0;
    std::vector<std::string> warnings;
};

// Reads one dump out of `bytes`, which may hold other bytes around it (chatter on a UART line,
// a file with a dump appended).
Trace read_trace(std::span<const std::byte> bytes);

// Whether `path` names a serial device rather than a dump file -- the CLI's serial-vs-file
// decision, declared here so the tests can pin it (I3): a path under /dev/, or a COM port
// name, "COM" followed only by one or more digits ("COM3", "COM12"; case-insensitive, and
// optionally the \\.\ prefix Windows needs from COM10 up). A file whose name merely starts
// with "com" (com_dump.bin, combined.prtr) is a file, not a port.
bool is_serial_path(std::string_view path);

} // namespace prism_trace
