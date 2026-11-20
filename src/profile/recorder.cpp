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

// Slot::id: a value unique per thread. Hosted: a hash of the thread id. A FreeRTOS port
// replaces this one function with the task handle, so task-switch events land on the same
// track as the task's marks.
std::uint64_t this_thread_id()
{
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

std::uint64_t next_serial()
{
    static std::atomic<std::uint64_t> serial{1};
    return serial.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

Recorder::Recorder(std::span<Event> storage, std::size_t threads)
    : threads_(std::min(threads, max_threads))
    , serial_(next_serial())
{
    if (threads_ == 0 || storage.empty())
        return;
    const std::size_t capacity = storage.size() / threads_; // split evenly; the rest is unused
    for (std::size_t i = 0; i < threads_; ++i) {
        slots_[i].ring = storage.data() + i * capacity;
        slots_[i].capacity = capacity;
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

void Recorder::pause() { paused_.store(true, std::memory_order_relaxed); }
void Recorder::resume() { paused_.store(false, std::memory_order_relaxed); }

Recorder::Slot* Recorder::claim_slot()
{
    struct CacheEntry {
        std::uint64_t serial;
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
    const std::uint64_t index = next_thread_.fetch_add(1, std::memory_order_relaxed);
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
    if (paused_.load(std::memory_order_relaxed))
        return;
    Slot* slot = claim_slot();
    if (slot == nullptr)
        return;
    const std::size_t capacity = slot->capacity;
    if (capacity == 0)
        return;
    // Only the owning thread writes its slot; the release stores publish the whole event.
    const std::uint64_t next = slot->next.load(std::memory_order_relaxed);
    if (next >= capacity) // the ring was already full: the oldest event is overwritten
        slot->lost.store(next + 1 - capacity, std::memory_order_relaxed);
    slot->ring[next % capacity] = Event{platform::profile_ticks(), name, value, kind};
    slot->count.store(next + 1 < capacity ? next + 1 : capacity, std::memory_order_release);
    slot->next.store(next + 1, std::memory_order_release);
}

} // namespace prism::profile
