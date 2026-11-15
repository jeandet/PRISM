#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

// Handoffs between a real-time thread and a UI thread, without a lock and without allocating.
// PRISM's own core::Shared<T> allocates on set() (it stores a shared_ptr) and falls back to a
// lock on platforms where that is not atomic, so it cannot be used from an audio callback.
// These two are what an audio path needs instead; both are single-producer/single-consumer.
namespace prism::synth {

// The newest value, whole. The writer never waits for the reader: publishing costs one copy
// and one atomic exchange. The reader always gets a complete value -- the newest published, or
// the one it already had if nothing new arrived -- and never a half-written one.
//
// Three slots rotate ownership: the writer fills `back_`, publishes it by swapping it with the
// middle slot, and the reader claims the middle slot when it carries the "new" flag, trading in
// the slot it was reading. No slot is ever both written and read.
template <typename T>
class RtLatest {
    static_assert(std::is_trivially_copyable_v<T>,
                  "RtLatest hands values across threads by copying bytes: keep T trivially copyable");

public:
    RtLatest() = default;
    explicit RtLatest(const T& initial)
        : slots_{Slot{initial}, Slot{initial}, Slot{initial}} {}

    // Producer side. Never blocks, never allocates.
    void publish(const T& value) noexcept {
        slots_[back_].value = value;
        back_ = middle_.exchange(back_ | kNew, std::memory_order_acq_rel) & kIndexMask;
    }

    // Consumer side. Returns the newest published value.
    T read() noexcept {
        if ((middle_.load(std::memory_order_acquire) & kNew) == 0u) return slots_[front_].value;
        const std::uint32_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = previous & kIndexMask;
        return slots_[front_].value;
    }

private:
    static constexpr std::uint32_t kNew = 4;        // flag bit; the slots are 0..2
    static constexpr std::uint32_t kIndexMask = 3;

    // One cache line per slot: the writer touching `back_` must not invalidate the reader's line.
    struct alignas(64) Slot {
        T value{};
    };

    std::array<Slot, 3> slots_{};
    std::atomic<std::uint32_t> middle_{1};
    std::uint32_t back_ = 0;    // writer's alone
    std::uint32_t front_ = 2;   // reader's alone
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

// The newest N samples, for display. The writer appends a block and never waits; the reader
// copies a window ending at the last write. Each sample is a relaxed atomic, so a reader can
// never race the writer (no UB, whatever the timing), and the writer never locks.
// simplify: a window read while the writer laps it can mix samples from two blocks, i.e. a
// display glitch now and then, not a corrupt sample. Upgrade path: a per-block sequence
// counter the reader validates after copying, retrying when it changed.
class SampleRing {
public:
    explicit SampleRing(std::size_t capacity = 1u << 14) {
        // A power of two, so wrapping is a mask.
        std::size_t size = 1;
        while (size * 2 <= capacity) size *= 2;
        buffer_ = std::make_unique<std::atomic<float>[]>(size);   // zero-initialized: silence
        mask_ = size - 1;
    }

    std::size_t capacity() const { return mask_ + 1; }

    // Writer side: append `samples`. Never blocks, never allocates.
    void write(std::span<const float> samples) noexcept {
        std::uint64_t at = written_;
        for (float sample : samples)
            buffer_[static_cast<std::size_t>(at++) & mask_].store(sample, std::memory_order_relaxed);
        written_ = at;
        published_.store(at, std::memory_order_release);
    }

    // Reader side: copy the newest `out.size()` samples into `out`, oldest first. Returns how
    // many were copied -- fewer only while fewer have been written.
    std::size_t read_newest(std::span<float> out) const noexcept {
        const auto end = published_.load(std::memory_order_acquire);
        const auto available = std::min<std::uint64_t>(end, capacity());
        const auto count = std::min<std::uint64_t>(out.size(), available);
        const auto start = end - count;
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto slot = static_cast<std::size_t>(start + i) & mask_;
            out[static_cast<std::size_t>(i)] = buffer_[slot].load(std::memory_order_relaxed);
        }
        return static_cast<std::size_t>(count);
    }

private:
    static_assert(std::atomic<float>::is_always_lock_free, "a callback must not take a lock");

    std::unique_ptr<std::atomic<float>[]> buffer_{};
    std::size_t mask_ = 0;
    std::uint64_t written_ = 0;                        // writer's alone
    std::atomic<std::uint64_t> published_{0};          // what the reader may see
};

} // namespace prism::synth
