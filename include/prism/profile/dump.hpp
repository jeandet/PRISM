#pragma once

// dump(): the recorder's contents as the framed binary trace of trace_format.hpp, over any
// io::ByteStream -- a file on the desktop (io::FileStream), a UART on a board
// (doc/design/profiling.md). Header-only, so it builds everywhere, bare metal included; the
// stream decides where the bytes go. The dump pauses the recorder for its duration and
// resumes it on every exit path (profile_detail::PauseGuard), so the marks made while a slow
// stream drains are dropped. It writes through a fixed 512-byte output buffer: a dump
// allocates nothing.
#include <prism/io/byte_stream.hpp>
#include <prism/profile/recorder.hpp>
#include <prism/profile/trace_format.hpp>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace prism::profile {
namespace profile_detail {

// Pauses the recorder on construction and resumes it on destruction, so every exit path of
// dump() -- the stream's error included -- leaves the recorder running again.
class PauseGuard {
public:
    explicit PauseGuard(const Recorder& r) : recorder_(&r) { recorder_->pause(); }
    ~PauseGuard() { recorder_->resume(); }
    PauseGuard(const PauseGuard&) = delete;
    PauseGuard& operator=(const PauseGuard&) = delete;

private:
    const Recorder* recorder_;
};

// Frames chunks (trace_format.hpp) over `out` through a fixed 512-byte buffer. begin() writes
// the type and payload length, both fed to the chunk's CRC -- the length is streamed before
// the payload, so it must be known up front, which is why the chunk writers size their
// payload before calling begin(). put() writes payload bytes, end() the CRC. The first write
// error is remembered; every call after it is a no-op.
template <io::ByteStream S>
class ChunkWriter {
public:
    static constexpr std::size_t buffer_size = 512;

    explicit ChunkWriter(S& out) : out_(&out) {}

    // Bytes outside any chunk (the sync marker): buffered, not CRC'd.
    void raw(std::span<const std::byte> bytes) { buffer(bytes); }

    void begin(Chunk type, std::uint32_t payload_len)
    {
        if (error_)
            return;
        crc_ = 0xFFFFFFFFu;
        std::array<std::byte, 5> head{};
        head[0] = static_cast<std::byte>(type);
        for (std::size_t i = 0; i < 4; ++i)
            head[1 + i] = static_cast<std::byte>((payload_len >> (8 * i)) & 0xFFu);
        crc_ = crc32_update(crc_, head);
        buffer(head);
    }

    void put(std::span<const std::byte> bytes)
    {
        if (error_)
            return;
        crc_ = crc32_update(crc_, bytes);
        buffer(bytes);
    }

    // One little-endian integer, whatever the target's pointer or int sizes.
    template <std::unsigned_integral UInt>
    void put_uint(UInt value)
    {
        std::array<std::byte, sizeof(UInt)> bytes{};
        for (std::size_t i = 0; i < sizeof(UInt); ++i)
            bytes[i] = static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xFFu);
        put(bytes);
    }

    void end()
    {
        if (error_)
            return;
        const std::uint32_t crc = crc_ ^ 0xFFFFFFFFu;
        std::array<std::byte, 4> tail{};
        for (std::size_t i = 0; i < 4; ++i)
            tail[i] = static_cast<std::byte>((crc >> (8 * i)) & 0xFFu);
        buffer(tail);
    }

    // The buffer's tail: without this a dump shorter than buffer_size never reaches the
    // stream (and a failing stream's error would never surface).
    void flush()
    {
        if (len_ == 0 || error_)
            return;
        if (auto res = out_->write(std::span{buf_.data(), len_}); !res) {
            if (!error_)
                error_ = res.error();
        }
        len_ = 0;
    }

    const std::optional<std::error_code>& error() const { return error_; }

private:
    void buffer(std::span<const std::byte> bytes)
    {
        while (!bytes.empty() && !error_) {
            const std::size_t n = std::min(bytes.size(), buffer_size - len_);
            std::copy_n(bytes.begin(), n, buf_.data() + len_);
            len_ += n;
            bytes = bytes.subspan(n);
            if (len_ == buffer_size)
                flush();
        }
    }

    S* out_;
    std::array<std::byte, buffer_size> buf_{};
    std::size_t len_ = 0;
    std::uint32_t crc_ = 0xFFFFFFFFu;
    std::optional<std::error_code> error_;
};

// The fixed dedup table of site pointers for one events chunk: a linear scan over at most
// max_sites_per_chunk entries -- the dump allocates nothing.
class SiteTable {
public:
    // False when the table is full and `site` is not in it; the caller then ends the chunk
    // before the event that would overflow it.
    bool add(const Site* site)
    {
        for (std::size_t i = 0; i < count_; ++i)
            if (ptrs_[i] == site)
                return true;
        if (count_ == ptrs_.size())
            return false;
        ptrs_[count_++] = site;
        return true;
    }
    void clear() { count_ = 0; }
    std::size_t count() const { return count_; }
    const Site* at(std::size_t i) const { return ptrs_[i]; }

private:
    std::array<const Site*, max_sites_per_chunk> ptrs_{};
    std::size_t count_ = 0;
};

// The view's event at flat index i, oldest first, across the two spans.
inline const Event& event_at(const Recorder::ThreadView& tv, std::size_t i)
{
    return i < tv.first.size() ? tv.first[i] : tv.second[i - tv.first.size()];
}

// strlen of one of a site's strings, cut at 65535 (the sites chunk's u16 length).
inline std::size_t site_string_length(const char* s)
{
    return std::min(std::strlen(s), std::size_t{65535});
}

// The sites-chunk payload bytes of one site: pointer u64 | line u32 | name, function, file
// (each u16 length + bytes).
inline std::size_t site_payload_size(const Site& site)
{
    return 8 + 4 + (2 + site_string_length(site.name)) + (2 + site_string_length(site.function)) +
           (2 + site_string_length(site.file));
}

// How many of tv's events from flat index `start` go into the next events chunk: at most
// max_events_per_chunk, and no site beyond the table's capacity. The sites of the chosen
// events are added to `sites`, which the chunk's sites chunk then carries exactly.
inline std::size_t plan_events_chunk(const Recorder::ThreadView& tv, std::size_t start, SiteTable& sites)
{
    const std::size_t total = tv.first.size() + tv.second.size();
    std::size_t count = 0;
    for (std::size_t i = start; i < total && count < max_events_per_chunk; ++i) {
        const Event& e = event_at(tv, i);
        if (!sites.add(e.site))
            break; // the table is full: end the chunk before this event
        ++count;
    }
    return count;
}

template <io::ByteStream S>
void write_header_chunk(ChunkWriter<S>& w, std::size_t thread_count, std::uint64_t threads_dropped)
{
    w.begin(Chunk::header, static_cast<std::uint32_t>(2 + 1 + 8 + 8 + 8));
    w.put_uint(format_version);
    w.put_uint(static_cast<std::uint8_t>(sizeof(void*)));
    w.put_uint(prism::platform::profile_ticks_per_second());
    w.put_uint(static_cast<std::uint64_t>(thread_count));
    w.put_uint(threads_dropped);
    w.end();
}

template <io::ByteStream S>
void write_thread_chunk(ChunkWriter<S>& w, const Recorder::ThreadView& tv)
{
    w.begin(Chunk::thread, static_cast<std::uint32_t>(8 + 8 + 1 + tv.name.size()));
    w.put_uint(tv.id);
    w.put_uint(tv.lost);
    w.put_uint(static_cast<std::uint8_t>(tv.name.size()));
    w.put(io::as_bytes(tv.name));
    w.end();
}

// One of a site's strings in the sites chunk: u16 length (cut at 65535) + bytes.
template <io::ByteStream S>
void put_site_string(ChunkWriter<S>& w, const char* s)
{
    const std::size_t len = site_string_length(s);
    w.put_uint(static_cast<std::uint16_t>(len));
    w.put(io::as_bytes(std::string_view{s, len}));
}

// One site of a sites chunk: pointer u64 | line u32 | name, function, file.
template <io::ByteStream S>
void write_site(ChunkWriter<S>& w, const Site& site)
{
    w.put_uint(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&site)));
    w.put_uint(site.line);
    put_site_string(w, site.name);
    put_site_string(w, site.function);
    put_site_string(w, site.file);
}

template <io::ByteStream S>
void write_sites_chunk(ChunkWriter<S>& w, const SiteTable& sites)
{
    std::size_t payload = 0;
    for (std::size_t i = 0; i < sites.count(); ++i)
        payload += site_payload_size(*sites.at(i));
    w.begin(Chunk::sites, static_cast<std::uint32_t>(payload)); // the length streams before the payload
    for (std::size_t i = 0; i < sites.count(); ++i)
        write_site(w, *sites.at(i));
    w.end();
}

template <io::ByteStream S>
void write_events_chunk(ChunkWriter<S>& w, const Recorder::ThreadView& tv, std::size_t start, std::size_t count)
{
    w.begin(Chunk::events, static_cast<std::uint32_t>(8 + count * event_wire_size));
    w.put_uint(tv.id);
    for (std::size_t k = 0; k < count; ++k) {
        const Event& e = event_at(tv, start + k);
        w.put_uint(e.ticks);
        // The site pointer; never null (frame() records a site of its own).
        w.put_uint(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(e.site)));
        w.put_uint(static_cast<std::uint32_t>(e.value)); // i32: its two's-complement bits as u32 LE
        w.put_uint(static_cast<std::uint8_t>(e.kind));
    }
    w.end();
}

// One or more (sites, events) pairs for one thread: the sites chunk before an events chunk
// carries the distinct site pointers that events chunk uses. Even a thread with no events
// gets one (empty) pair.
template <io::ByteStream S>
void dump_thread_pairs(ChunkWriter<S>& w, const Recorder::ThreadView& tv)
{
    const std::size_t total = tv.first.size() + tv.second.size();
    SiteTable sites;
    std::size_t start = 0;
    do {
        if (w.error())
            return;
        const std::size_t count = plan_events_chunk(tv, start, sites);
        write_sites_chunk(w, sites);
        write_events_chunk(w, tv, start, count);
        start += count;
        sites.clear();
    } while (start < total);
}

} // namespace profile_detail

// Writes r's dump to `out`: the sync marker, a header chunk, one thread chunk per thread,
// then per thread one or more (sites, events) chunk pairs, then an end chunk. While paused,
// a mark already in flight is left out of the view (for_each_thread), so the dump never reads
// a half-written event and never waits for a marking thread.
template <io::ByteStream S>
std::expected<void, std::error_code> dump(const Recorder& r, S& out)
{
    const profile_detail::PauseGuard paused{r}; // resumes on every exit path, errors included
    profile_detail::ChunkWriter<S> w{out};
    w.raw(std::span<const std::byte>{sync_marker});
    // One snapshot of the threads, in a fixed array: a slot claimed while the dump runs (a
    // thread's first mark during the pause) must not change the header's thread count
    // mid-dump. for_each_thread yields at most Recorder::max_threads views, so this cannot
    // overflow, and the rings the spans point into stay untouched while paused.
    std::array<Recorder::ThreadView, Recorder::max_threads> views{};
    std::size_t thread_count = 0;
    r.for_each_thread([&](const Recorder::ThreadView& tv) { views[thread_count++] = tv; });
    profile_detail::write_header_chunk(w, thread_count, r.threads_dropped());
    for (std::size_t i = 0; i < thread_count && !w.error(); ++i)
        profile_detail::write_thread_chunk(w, views[i]);
    for (std::size_t i = 0; i < thread_count && !w.error(); ++i)
        profile_detail::dump_thread_pairs(w, views[i]);
    w.begin(Chunk::end, 0);
    w.end();
    w.flush(); // the buffer's tail, so a short dump reaches the stream too
    if (const auto& err = w.error())
        return std::unexpected(*err);
    return {};
}

#if defined(PRISM_PROFILING)
// Dumps global() to the file at `path` (through io::FileStream). Declared only with
// PRISM_PROFILING; defined only on a hosted build (src/profile/dump.cpp, gated on
// host_machine.system() != 'none' in src/meson.build -- bare metal has no file system).
std::expected<void, std::error_code> dump_to_file(std::string_view path);
#endif

} // namespace prism::profile
