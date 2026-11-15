#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

// The cross-thread handoff for a real-time producer: an audio callback, a DAQ interrupt
// thread. Shared<T> allocates on set() (it publishes a shared_ptr) and Channel<T> allocates per
// send, so neither may run where a stall is a glitch. Latest<T> never locks and never allocates.
// Single producer, single consumer: pair it with a Shared<T> or a polling tick on the consumer
// side to reach the logic thread.
namespace prism::core {

// The newest value, whole. The writer never waits for the reader: publishing costs one copy
// and one atomic exchange. The reader always gets a complete value -- the newest published, or
// the one it already had if nothing new arrived -- and never a half-written one.
//
// Three slots rotate ownership: the writer fills `back_`, publishes it by swapping it with the
// middle slot, and the reader claims the middle slot when it carries the "new" flag, trading in
// the slot it was reading. No slot is ever both written and read.
template <typename T>
class Latest {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Latest hands values across threads by copying bytes: keep T trivially copyable");

public:
    Latest() = default;
    explicit Latest(const T& initial)
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

} // namespace prism::core
