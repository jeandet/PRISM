#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// The audio thread's display tap: its newest samples, handed to the analysis thread without a
// lock and without allocating. (The settings travel the other way in a core::Latest.)
namespace prism::synth {

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
