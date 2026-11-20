#pragma once

// The post-mortem recorder behind the PRISM_PROFILE_* marks (profile.hpp,
// doc/design/profiling.md): per-thread rings of fixed-size events carved out of one storage
// region. A mark writes only its thread's ring -- no lock and no allocation once the thread
// has claimed its ring on its first mark -- and records a single pointer: its call site, a
// static constexpr Site built by the PRISM_PROFILE_* macros (profile.hpp), never copied,
// hashed or formatted. A full ring overwrites the oldest events and counts them in
// ThreadView::lost (a flight recorder: the end of a run is what survives). Read back with
// for_each_thread, oldest first, as up to two spans -- a mark in flight when the view is
// taken is left out of it, never read half-written (see for_each_thread).

#include <prism/profile/trace_format.hpp>

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <source_location>
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

// A call site: what a mark points at. Each PRISM_PROFILE_* macro builds one as a static
// constexpr (make_site below), so the event records one pointer and the dump sends the
// site's strings once -- the host tool needs no ELF.
struct Site {
    const char* name;
    const char* function;
    const char* file;
    std::uint32_t line;
};

// The Site of a call: std::source_location::current() as this consteval function's default
// argument gives the location of the call -- for the macros, their expansion, i.e. the
// mark's call site. `name` must be a constant expression (a string literal): a non-constant
// name does not compile.
consteval Site make_site(const char* name,
                         std::source_location loc = std::source_location::current())
{
    return Site{name, loc.function_name(), loc.file_name(), loc.line()};
}


struct Event {
    std::uint64_t ticks;
    const Site* site;   // the mark's call site: recorded by pointer, never copied or formatted
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

    void begin(const Site& site);
    void end(const Site& site);
    void instant(const Site& site);
    void counter(const Site& site, std::int32_t v);
    // The ring keeps the site's address until the dump: a temporary would dangle.
    void begin(const Site&&) = delete;
    void end(const Site&&) = delete;
    void instant(const Site&&) = delete;
    void counter(const Site&&, std::int32_t) = delete;
    // Records a site of its own (a static constexpr in recorder.cpp, named "frame"), so no
    // event has a null site.
    void frame();
    // Names the calling thread on the timeline: copied into its slot, cut at 16 characters, so
    // a finished thread leaves no dangling pointer behind.
    void name_thread(std::string_view name);
    // Marks become no-ops until resume(). Const because dump() holds a const Recorder& while
    // it pauses it; the seq_cst store pairs with record()'s seq_cst load of it, so a mark that
    // got past the check before the pause is seen by for_each_thread as in flight.
    void pause() const;
    void resume() const;

    struct ThreadView {
        std::string_view name;
        std::uint64_t id;
        std::uint64_t lost;                   // events the full ring has overwritten
        std::span<const Event> first, second; // the ring, oldest first, as up to two spans
    };

    // Calls f(const ThreadView&) once per thread that has a ring, in slot order. The ring is
    // given oldest first as up to two spans: when it wrapped, [head, capacity) then [0, head);
    // otherwise [0, head).
    //
    // A mark in flight when the view is taken is left out, never read half-written: with a
    // wrapped ring the event being written at ring[head] is the oldest, so the view starts one
    // past it; not wrapped, that write is past the view and nothing is dropped. The reader
    // never waits for the marking thread. The view is therefore exact only while the recorder
    // is paused (dump() pauses it) or while no thread is marking.
    template <class F>
    void for_each_thread(F&& f) const
    {
        for (std::size_t i = 0; i < threads_; ++i) {
            const Slot& s = slots_[i];
            if (!s.used.load(std::memory_order_acquire))
                continue;
            const std::uint32_t capacity = s.capacity;
            // writing first (seq_cst, Dekker-paired with record()'s writing store and pause()'s
            // paused store): a mark that passed the paused check before pause() is seen here as
            // writing. Loaded after it, head/wrapped (acquire) already include any mark that
            // finished before, so the newest event never shows up as the oldest.
            const bool writing = s.writing.load(std::memory_order_seq_cst);
            const std::uint32_t head = s.head.load(std::memory_order_acquire);
            const bool wrapped = s.wrapped.load(std::memory_order_acquire);
            ThreadView tv;
            tv.name = s.name.data();
            tv.id = s.id.load(std::memory_order_relaxed);
            tv.lost = s.lost.load(std::memory_order_relaxed);
            if (capacity > 0 && wrapped) {
                std::uint32_t first_begin = head;
                if (writing) // drop ring[head], the in-flight write: the oldest once wrapped
                    first_begin = head + 1 == capacity ? capacity : head + 1;
                tv.first = first_begin < capacity
                               ? std::span<const Event>(s.ring + first_begin, capacity - first_begin)
                               : std::span<const Event>{};
                tv.second = head != 0 ? std::span<const Event>(s.ring, head)
                                      : std::span<const Event>{};
            } else {
                tv.first = head != 0 ? std::span<const Event>(s.ring, head)
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
        std::atomic<std::uintptr_t> id{0};   // a FreeRTOS task handle fits
        std::atomic<std::uint32_t> head{0};  // next write index, always < capacity
        std::atomic<bool> wrapped{false};    // the ring has been filled once
        // The in-flight flag: record() sets it (seq_cst) before its paused check and clears it
        // after the write, so for_each_thread can leave the event being written out of its view.
        std::atomic<bool> writing{false};
        // simplify: saturates at UINT32_MAX -- a wider count would need a lock-free 64-bit
        // atomic, which a Cortex-M7 does not have
        std::atomic<std::uint32_t> lost{0};
        std::array<char, 17> name{};         // name_thread's copy: 16 characters + NUL
        Event* ring = nullptr;
        std::uint32_t capacity = 0;
    };

    Slot* claim_slot(); // this thread's ring, claimed on its first mark
    void record(Kind kind, const Site* site, std::int32_t value);

    std::size_t threads_;
    std::uint32_t serial_; // keys the threads' slot cache: a new recorder at an old address is a new key
    std::array<Slot, max_threads> slots_{};
    std::atomic<std::uint32_t> next_thread_{0}; // slot table fill counter
    std::atomic<std::uint32_t> threads_dropped_{0};
    mutable std::atomic<bool> paused_{false}; // mutable: pause()/resume() are const
};

// A Cortex-M7 has no lock-free 64-bit atomics (they would link libatomic, which locks), so every
// atomic in Recorder is 32-bit or pointer-sized.
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);

#if defined(PRISM_PROFILING)
// The global recorder behind the PRISM_PROFILE_* macros: one static region of
// PRISM_PROFILING_EVENTS * PRISM_PROFILING_THREADS events in PRISM_PROFILING_THREADS rings
// (src/profile/global.cpp, compiled only with -Dprofiling=enabled).
Recorder& global();
#endif

} // namespace prism::profile
