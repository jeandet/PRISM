// The recorder behind the PRISM_PROFILE_* marks (include/prism/profile/recorder.hpp,
// doc/design/profiling.md): per-thread rings carved out of one storage region. A mark writes
// only its thread's ring -- the slot comes from a thread_local cache keyed by the recorder,
// filled from an atomic counter on the thread's first mark -- so past that first mark a mark
// takes no lock and allocates nothing.
#include <prism/profile/recorder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <thread>

namespace prism::profile {
namespace {

// Slot::id: a value unique per thread. Hosted: a hash of the thread id. This lives in an
// anonymous namespace, so a port cannot replace it by linking something else in; Task 7 (the
// FreeRTOS task switches) will move it behind the port, so the task handle lands on the same
// track as the task's marks.
std::uintptr_t this_thread_id()
{
    return static_cast<std::uintptr_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// Keys the threads' slot cache. 32-bit on purpose, like every atomic here: no lock-free 64-bit
// atomics (see the static_asserts in recorder.hpp). Wraps after 2^32 recorders in one process.
std::uint32_t next_serial()
{
    static std::atomic<std::uint32_t> serial{1};
    return serial.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

Recorder::Recorder(std::span<Event> storage, std::size_t threads)
    : threads_(std::min(threads, max_threads))
    , serial_(next_serial())
{
    if (threads_ == 0 || storage.empty())
        return;
    // One division here is fine: capacity is computed once, then record() only compares against
    // it. Clamped to the slot's 32-bit capacity.
    const std::size_t cap = std::min(storage.size() / threads_, static_cast<std::size_t>(UINT32_MAX));
    for (std::size_t i = 0; i < threads_; ++i) {
        slots_[i].ring = storage.data() + i * cap; // split evenly; the rest is unused
        slots_[i].capacity = static_cast<std::uint32_t>(cap);
    }
}

void Recorder::begin(const char* name) { record(Kind::begin, name, 0); }
void Recorder::end(const char* name) { record(Kind::end, name, 0); }
void Recorder::instant(const char* name) { record(Kind::instant, name, 0); }
void Recorder::counter(const char* name, std::int32_t v) { record(Kind::counter, name, v); }
void Recorder::frame() { record(Kind::frame, nullptr, 0); }

void Recorder::name_thread(std::string_view name)
{
    Slot* slot = claim_slot();
    if (slot == nullptr)
        return;
    const std::size_t n = std::min(name.size(), slot->name.size() - 1); // 16 characters + NUL
    std::copy_n(name.begin(), n, slot->name.begin());
    slot->name[n] = '\0';
    slot->used.store(true, std::memory_order_release); // publish the name to for_each_thread
}

void Recorder::pause() const { paused_.store(true, std::memory_order_seq_cst); }
// Release: the dump's reads of the rings happen before the next mark overwrites them.
void Recorder::resume() const { paused_.store(false, std::memory_order_release); }

Recorder::Slot* Recorder::claim_slot()
{
    struct CacheEntry {
        std::uint32_t serial;
        Slot* slot; // null: this thread is beyond the table, already counted in threads_dropped_
    };
    // Keyed by the recorder's serial, so several recorders can live in one process (the tests
    // use one per case). simplify: 16 entries, the oldest overwritten; a thread that marks
    // into more than 16 live recorders in turn takes a second ring in an evicted one.
    static thread_local std::array<CacheEntry, 16> cache{};
    static thread_local std::size_t oldest = 0;
    for (const CacheEntry& e : cache)
        if (e.serial == serial_)
            return e.slot;
    Slot* slot = nullptr;
    const std::uint32_t index = next_thread_.fetch_add(1, std::memory_order_relaxed);
    if (index < threads_) {
        slot = &slots_[index];
        slot->id.store(this_thread_id(), std::memory_order_relaxed);
        slot->used.store(true, std::memory_order_release);
    } else {
        threads_dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    cache[oldest] = CacheEntry{serial_, slot};
    oldest = (oldest + 1) % cache.size();
    return slot;
}

void Recorder::record(Kind kind, const char* name, std::int32_t value)
{
    Slot* slot = claim_slot();
    if (slot == nullptr)
        return;
    const std::uint32_t capacity = slot->capacity;
    if (capacity == 0)
        return;
    // The in-flight flag, Dekker-paired with pause()'s seq_cst store: a mark that gets past
    // the paused check below is guaranteed to be seen by for_each_thread as writing, so a dump
    // never reads the event this mark is writing (see for_each_thread).
    slot->writing.store(true, std::memory_order_seq_cst);
    if (paused_.load(std::memory_order_seq_cst)) {
        slot->writing.store(false, std::memory_order_release);
        return;
    }
    // Only the owning thread writes its slot, so plain load/store suffice; the release stores
    // of head/wrapped publish the event just written. No division: the write index wraps at
    // capacity with a compare.
    const std::uint32_t head = slot->head.load(std::memory_order_relaxed);
    const bool was_wrapped = slot->wrapped.load(std::memory_order_relaxed);
    slot->ring[head] = Event{platform::profile_ticks(), name, value, kind};
    std::uint32_t next = head + 1;
    if (next == capacity) {
        next = 0;
        slot->wrapped.store(true, std::memory_order_release);
    }
    slot->head.store(next, std::memory_order_release);
    if (was_wrapped) {
        // The ring was already full, so this write overwrote the oldest event. Owner-thread
        // load + store, no RMW.
        const std::uint32_t lost = slot->lost.load(std::memory_order_relaxed);
        if (lost != UINT32_MAX) // simplify: saturates at UINT32_MAX (see Slot::lost)
            slot->lost.store(lost + 1, std::memory_order_relaxed);
    }
    slot->writing.store(false, std::memory_order_release);
}

} // namespace prism::profile
