#pragma once

// The post-mortem recorder behind the PRISM_PROFILE_* marks (profile.hpp,
// doc/design/profiling.md): per-thread rings of fixed-size events carved out of one storage
// region. A mark writes only its thread's ring -- no lock and no allocation once the thread
// has claimed its ring on its first mark; a full ring overwrites the oldest events and counts
// them in ThreadView::lost (a flight recorder: the end of a run is what survives). Read back
// with for_each_thread, oldest first, as up to two spans.

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace prism::platform {

// The clock behind Recorder's ticks: monotonic, at most 1 µs resolution, cheap to read.
// Defined by src/profile/steady_clock.cpp on a hosted system (std::chrono::steady_clock), by
// the port on bare metal (the DWT cycle counter extended to 64 bits).
std::uint64_t profile_ticks();
std::uint64_t profile_ticks_per_second();

} // namespace prism::platform

namespace prism::profile {

enum class Kind : std::uint8_t { begin, end, instant, counter, frame };

struct Event {
    std::uint64_t ticks;
    const char* name;   // a string literal: recorded by pointer, never copied or formatted
    std::int32_t value; // the counter's sample
    Kind kind;
};

class Recorder {
public:
    // The slot table is a fixed array -- no heap allocation anywhere in Recorder (the design's
    // "all rings live in one static region"). The constructor clamps `threads` to this.
    static constexpr std::size_t max_threads = 64;

    // `storage` is split evenly between `threads` rings. A thread beyond the table records
    // nothing and is counted in threads_dropped().
    Recorder(std::span<Event> storage, std::size_t threads);
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void begin(const char* name);
    void end(const char* name);
    void instant(const char* name);
    void counter(const char* name, std::int32_t v);
    void frame();
    // Names the calling thread on the timeline: copied into its slot, cut at 16 characters, so
    // a finished thread leaves no dangling pointer behind.
    void name_thread(std::string_view name);
    void pause(); // marks become no-ops until resume()
    void resume();

    struct ThreadView {
        std::string_view name;
        std::uint64_t id;
        std::uint64_t lost;                   // events the full ring has overwritten
        std::span<const Event> first, second; // the ring, oldest first, as up to two spans
    };

    // Calls f(const ThreadView&) once per thread that has a ring, in slot order. The ring is
    // given oldest first as up to two spans: when it wrapped, [next % capacity, capacity) then
    // [0, next % capacity).
    template <class F>
    void for_each_thread(F&& f) const
    {
        for (std::size_t i = 0; i < threads_; ++i) {
            const Slot& s = slots_[i];
            if (!s.used.load(std::memory_order_acquire))
                continue;
            const std::uint64_t capacity = s.capacity;
            // Acquire on next: record()'s release store of it publishes the whole event ring.
            const std::uint64_t next = s.next.load(std::memory_order_acquire);
            ThreadView tv;
            tv.name = s.name.data();
            tv.id = s.id.load(std::memory_order_relaxed);
            tv.lost = s.lost.load(std::memory_order_relaxed);
            if (capacity > 0 && next > capacity) {
                const auto off = static_cast<std::size_t>(next % capacity);
                tv.first = std::span<const Event>(s.ring + off, capacity - off);
                tv.second = std::span<const Event>(s.ring, off);
            } else {
                const std::uint64_t count = s.count.load(std::memory_order_acquire);
                tv.first = count != 0 ? std::span<const Event>(s.ring, static_cast<std::size_t>(count))
                                     : std::span<const Event>{};
                tv.second = {};
            }
            f(tv);
        }
    }

    std::uint64_t threads_dropped() const { return threads_dropped_.load(std::memory_order_relaxed); }

private:
    struct Slot {
        std::atomic<bool> used{false};
        std::atomic<std::uint64_t> id{0};
        std::atomic<std::uint64_t> lost{0};
        std::atomic<std::uint64_t> next{0};  // ring write cursor
        std::atomic<std::uint64_t> count{0}; // events held, <= capacity
        std::array<char, 17> name{};         // name_thread's copy: 16 characters + NUL
        Event* ring = nullptr;
        std::size_t capacity = 0;
    };

    Slot* claim_slot(); // this thread's ring, claimed on its first mark
    void record(Kind kind, const char* name, std::int32_t value);

    std::size_t threads_;
    std::uint64_t serial_; // keys the threads' slot cache: a new recorder at an old address is a new key
    std::array<Slot, max_threads> slots_{};
    std::atomic<std::uint64_t> next_thread_{0}; // slot table fill counter
    std::atomic<std::uint64_t> threads_dropped_{0};
    std::atomic<bool> paused_{false};
};

#if defined(PRISM_PROFILING)
// The global recorder behind the PRISM_PROFILE_* macros: one static region of
// PRISM_PROFILING_EVENTS * PRISM_PROFILING_THREADS events in PRISM_PROFILING_THREADS rings
// (src/profile/global.cpp, compiled only with -Dprofiling=enabled).
Recorder& global();
#endif

} // namespace prism::profile
