#pragma once

// Per-frame timing, summarized once a second into the line the runtime hands the app (FrameContext::on_stats):
// "24 fps · CPU 74% · heap 230/420 KB · stacks 120/384 KB · 412 allocs/frame · update 1.20 ·
// snapshot 9.10 · draw 22.40 · present 0.30 ms" (averages; allocations are heap allocations by
// any thread, per frame drawn). CPU and memory only when the platform reports them.

#include <chrono>
#include <cstdint>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace prism::platform {

// What the platform reads at one instant: the idle task's running time since boot (the CPU
// load is the share of a second it did not run) and the two heaps in use.
struct SystemCounters {
    std::uint64_t idle_us = 0;
    std::size_t heap_used = 0, heap_total = 0;     // malloc/new: PRISM and the app
    std::size_t stacks_used = 0, stacks_total = 0; // the RTOS heap: task stacks and kernel objects
};

// One finished frame: the app's loop hooks (e.g. FFT and plot data) and the snapshot build
// on the logic thread, then draw and present on the render thread.
struct StageTimes {
    std::chrono::microseconds update{}, snapshot{}, draw{}, present{};
};

// The platform's counters, read now; nullopt where it has none. Defined by whoever links the
// runtime (a port, or a test program).
std::optional<SystemCounters> system_counters();

class FrameStats {
public:
    // `allocations` is a running count of heap allocations (it may wrap around), read now.
    explicit FrameStats(std::chrono::steady_clock::time_point start, std::uint32_t allocations = 0,
                        std::optional<SystemCounters> system = {})
        : window_start_(start), window_allocations_(allocations), window_system_(system) {}

    void add(StageTimes t);
    // The summary of the last second once a second has passed since the previous one, then
    // starts a new second; nullopt before that. `allocations`: the running count, read now.
    // The line lives in this object's buffer until the next tick: no allocation.
    // `system`: the platform's counters read now, if it has any.
    std::optional<std::string_view> tick(std::chrono::steady_clock::time_point now,
                                         std::uint32_t allocations,
                                         std::optional<SystemCounters> system = {});

private:
    std::chrono::steady_clock::time_point window_start_;
    std::uint32_t window_allocations_;
    std::optional<SystemCounters> window_system_;
    StageTimes sum_{};
    int frames_ = 0;
    std::array<char, 256> line_{};
};

} // namespace prism::platform
