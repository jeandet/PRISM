#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"
#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>

// The platform hook the runtime reads: here, operator new's count (alloc_counter.hpp).
std::uint32_t prism::platform::allocation_count()
{
    return static_cast<std::uint32_t>(prism::test::allocations());
}

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {
prism::platform::StageTimes frame(int update_ms, int snapshot_ms, int draw_ms, int present_ms)
{
    return {std::chrono::milliseconds(update_ms), std::chrono::milliseconds(snapshot_ms),
            std::chrono::milliseconds(draw_ms), std::chrono::milliseconds(present_ms)};
}
} // namespace

TEST_CASE("frame stats: no line before a second has passed")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start};
    stats.add(frame(5, 2, 10, 1));
    CHECK_FALSE(stats.tick(start + 500ms, 0).has_value());
}

TEST_CASE("frame stats: one line per second with fps and average stage times")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start};
    stats.add(frame(5, 2, 10, 1));
    stats.add(frame(7, 4, 20, 1));
    CHECK(stats.tick(start + 1s, 0) == "2 fps \u00b7 0 allocs/frame \u00b7 update 6.00 \u00b7 snapshot 3.00 \u00b7 draw 15.00 \u00b7 present 1.00 ms");
}

TEST_CASE("frame stats: a second without frames reads 0 fps, not the previous numbers")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start};
    stats.add(frame(5, 2, 10, 1));
    (void)stats.tick(start + 1s, 0);
    CHECK(stats.tick(start + 2s, 5) == "0 fps \u00b7 - allocs/frame \u00b7 update - \u00b7 snapshot - \u00b7 draw - \u00b7 present - ms");
}

TEST_CASE("frame stats: sub-millisecond stages keep their precision")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start};
    stats.add({std::chrono::microseconds(120), std::chrono::microseconds(450),
               std::chrono::microseconds(8300), std::chrono::microseconds(30)});
    CHECK(stats.tick(start + 1s, 0) == "1 fps \u00b7 0 allocs/frame \u00b7 update 0.12 \u00b7 snapshot 0.45 \u00b7 draw 8.30 \u00b7 present 0.03 ms");
}

TEST_CASE("frame stats: allocations per frame since the previous line")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 1000};      // the allocation counter read at start
    stats.add(frame(1, 1, 1, 1));
    stats.add(frame(1, 1, 1, 1));
    CHECK(stats.tick(start + 1s, 1820)->starts_with("2 fps \u00b7 410 allocs/frame \u00b7"));
    stats.add(frame(1, 1, 1, 1));
    CHECK(stats.tick(start + 2s, 1820)->starts_with("1 fps \u00b7 0 allocs/frame \u00b7"));
}

TEST_CASE("frame stats: the allocation counter may wrap around")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0xFFFFFFF0u};
    stats.add(frame(1, 1, 1, 1));
    CHECK(stats.tick(start + 1s, 0x10u)->starts_with("1 fps \u00b7 32 allocs/frame \u00b7"));
}

TEST_CASE("frame stats: the line is written into the stats' own buffer, without allocating")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0};
    stats.add(frame(1, 2, 3, 4));
    (void)stats.tick(start + 1s, 0);        // warm-up
    stats.add(frame(1, 2, 3, 4));
    const auto before = prism::platform::allocation_count();
    const std::optional<std::string_view> line = stats.tick(start + 2s, 0);
    CHECK(prism::platform::allocation_count() - before == 0);
    REQUIRE(line.has_value());
    CHECK(line->starts_with("1 fps"));
}

namespace {
constexpr std::size_t kKB = 1024;
prism::platform::SystemCounters counters(std::uint64_t idle_us)
{
    return {.idle_us = idle_us,
            .heap_used = 230 * kKB, .heap_total = 420 * kKB,
            .stacks_used = 120 * kKB, .stacks_total = 384 * kKB};
}
} // namespace

TEST_CASE("frame stats: CPU load and memory lead the line when the platform reports them")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0, counters(1'000'000)};
    stats.add(frame(5, 2, 10, 1));
    // 250 ms of the second idle: the CPU was busy 75 % of it.
    CHECK(stats.tick(start + 1s, 0, counters(1'250'000)) ==
          "1 fps · CPU 75% · heap 230/420 KB · stacks 120/384 KB · 0 allocs/frame · update 5.00 · snapshot 2.00 · draw 10.00 · present 1.00 ms");
}

TEST_CASE("frame stats: the CPU load is measured over the last second only")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0, counters(0)};
    (void)stats.tick(start + 1s, 0, counters(100'000));               // 90 % busy
    CHECK(stats.tick(start + 2s, 0, counters(1'100'000))->starts_with("0 fps · CPU 0% ·"));
}

TEST_CASE("frame stats: an idle counter read late never shows a negative load")
{
    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0, counters(0)};
    CHECK(stats.tick(start + 1s, 0, counters(1'200'000))->starts_with("0 fps · CPU 0% ·"));
}
