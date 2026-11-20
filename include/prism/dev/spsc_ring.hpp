#pragma once

// A lock-free single-producer single-consumer ring of samples: the ADC's DMA interrupt
// pushes, the logic thread pops. Neither side blocks or allocates, so the producer may be an
// interrupt handler. Holds N - 1 items (one slot tells full from empty).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::dev {

template <typename T, std::size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    // Producer side. simplify: when full, the newest items are dropped (and counted): only the
    // consumer may move the read index; dropping the oldest would need a lock.
    void push(std::span<const T> items)
    {
        std::uint32_t w = write_.load(std::memory_order_relaxed);
        const std::uint32_t r = read_.load(std::memory_order_acquire);
        for (const T &item : items) {
            if (((w + 1) & kMask) == r) {
                dropped_.fetch_add(std::uint32_t(&items.back() - &item + 1), std::memory_order_relaxed);
                break;
            }
            slots_[w] = item;
            w = (w + 1) & kMask;
        }
        write_.store(w, std::memory_order_release);
    }

    // Consumer side: copies up to out.size() items, returns how many.
    std::size_t pop(std::span<T> out)
    {
        std::uint32_t r = read_.load(std::memory_order_relaxed);
        const std::uint32_t w = write_.load(std::memory_order_acquire);
        std::size_t n = 0;
        for (; n < out.size() && r != w; ++n) {
            out[n] = slots_[r];
            r = (r + 1) & kMask;
        }
        read_.store(r, std::memory_order_release);
        return n;
    }

    std::uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    static constexpr std::uint32_t kMask = N - 1;
    std::array<T, N> slots_{};
    std::atomic<std::uint32_t> write_{0}, read_{0}, dropped_{0};
};

} // namespace prism::dev
