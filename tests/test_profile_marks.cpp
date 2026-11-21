// PRISM's own marks (doc/design/profiling.md, "Marks PRISM places itself"; task 4): a frame_app
// run must leave the global recorder with a thread named "logic" carrying "snapshot" blocks
// and a thread named "render" carrying "draw" and "present" blocks, every begin paired with its
// end and properly nested. Fix round 1 adds: one "logic loop" block per loop turn on the logic
// thread (not one for the whole run), "present" never nested inside "draw" on the render
// thread, and FrameStats itself recording the "cpu %" / "heap KB" counters once per stats
// period. Registered in tests/meson.build only with -Dprofiling=enabled (the top-level
// meson.build's profiling_enabled): global() exists only there, so this file is never compiled
// without PRISM_PROFILING.
//
// global() is process-wide and one binary has one recorder, so events are selected by thread
// name and site name, never by position: each case here stays independent of any other case
// that might mark into the same recorder.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_app.hpp>
#include <prism/platform/frame_stats.hpp>
#include <prism/profile/profile.hpp>
#include <prism/profile/recorder.hpp>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The platform hooks the runtime reads, defined by whoever links it: here, operator new's count
// (alloc_counter.hpp) and no CPU or memory counters.
std::uint32_t prism::platform::allocation_count()
{
    return static_cast<std::uint32_t>(prism::test::allocations());
}

std::optional<prism::platform::SystemCounters> prism::platform::system_counters()
{
    return std::nullopt;
}

using namespace prism::core;
using prism::platform::FrameContext;
using prism::profile::Event;
using prism::profile::Kind;
using prism::profile::Recorder;

namespace {

constexpr int W = 120, H = 80;

// A model that changes on every loop turn, so the logic thread republishes (a "snapshot" block)
// every poll and a quit after 3 presents has seen at least 3 of them.
struct TickingModel {
    Field<int> count{0};
    void view(prism::app::ViewBuilder& vb) { vb.vstack(count); }
};

struct MemorySink final : prism::platform::FrameSink {
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(std::size_t(W) * H);
    std::atomic<int> presents{0};

    std::span<std::uint32_t> back() override { return pixels; }
    void present() override { ++presents; }
    Size size() const override { return {Width{float(W)}, Height{float(H)}}; }
};

struct NoTouch final : prism::platform::TouchSource {
    std::optional<Point> read() override { return std::nullopt; }
};

// One thread of the global recorder: its name and its ring, oldest first, as one flat vector.
struct ThreadEvents {
    std::string_view name;
    std::vector<Event> events;
};

std::vector<ThreadEvents> all_threads()
{
    std::vector<ThreadEvents> out;
    prism::profile::global().for_each_thread([&](const Recorder::ThreadView& tv) {
        ThreadEvents t;
        t.name = tv.name; // points into the recorder's slot: the global recorder outlives this
        t.events.assign(tv.first.begin(), tv.first.end());
        t.events.insert(t.events.end(), tv.second.begin(), tv.second.end());
        out.push_back(std::move(t));
    });
    return out;
}

bool has_thread(const std::vector<ThreadEvents>& threads, std::string_view name)
{
    for (const auto& t : threads)
        if (t.name == name)
            return true;
    return false;
}

// How many events of `kind` at a site named `site_name` the threads named `thread_name` carry.
std::size_t count_events(const std::vector<ThreadEvents>& threads, std::string_view thread_name,
                         std::string_view site_name, Kind kind)
{
    std::size_t n = 0;
    for (const auto& t : threads) {
        if (t.name != thread_name)
            continue;
        for (const Event& e : t.events)
            if (e.kind == kind && std::string_view(e.site->name) == site_name)
                ++n;
    }
    return n;
}

// Every begin on the threads named `thread_name` has its end, properly nested: walking the
// events oldest first, an end must close the newest open begin (same site), and nothing may be
// left open at the end. Instants, counters and frames are not blocks and are skipped.
void check_blocks_nested(const std::vector<ThreadEvents>& threads, std::string_view thread_name)
{
    for (const auto& t : threads) {
        if (t.name != thread_name)
            continue;
        std::vector<const prism::profile::Site*> open;
        for (const Event& e : t.events) {
            if (e.kind == Kind::begin) {
                open.push_back(e.site);
            } else if (e.kind == Kind::end) {
                REQUIRE_FALSE(open.empty()); // an end whose begin is gone (ring wrapped early)
                CHECK(open.back() == e.site); // the end closes the begin it belongs to
                open.pop_back();
            }
        }
        CHECK(open.empty()); // every begin has its end
    }
}

// On the threads named `thread_name`, no "present" block may open while a "draw" block is
// open: on the render thread the two are siblings (draw, then present), never nested.
void check_present_not_inside_draw(const std::vector<ThreadEvents>& threads, std::string_view thread_name)
{
    for (const auto& t : threads) {
        if (t.name != thread_name)
            continue;
        std::vector<const prism::profile::Site*> open;
        for (const Event& e : t.events) {
            if (e.kind == Kind::begin) {
                if (std::string_view(e.site->name) == "present")
                    for (const prism::profile::Site* s : open)
                        CHECK(std::string_view(s->name) != "draw"); // a "present" inside a "draw"
                open.push_back(e.site);
            } else if (e.kind == Kind::end && !open.empty() && open.back() == e.site) {
                open.pop_back();
            }
        }
    }
}

} // namespace

TEST_CASE("a frame_app run marks its logic and render threads on the global recorder")
{
    TickingModel model;
    MemorySink sink;
    NoTouch touch;
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        ctx.on_loop([&] {
            model.count.set(model.count.get() + 1); // dirty every turn: a snapshot per poll
            if (sink.presents >= 3)
                ctx.quit();
        });
    });
    REQUIRE(sink.presents >= 3); // 3 frames were drawn and presented

    const auto threads = all_threads();

    // The two threads the runtime names (step 2: name_thread in frame_runtime.cpp).
    CHECK(has_thread(threads, "logic"));
    CHECK(has_thread(threads, "render"));

    // The logic thread built at least one snapshot per presented frame.
    CHECK(count_events(threads, "logic", "snapshot", Kind::begin) >= 3);
    CHECK(count_events(threads, "logic", "snapshot", Kind::end) >= 3);

    // The render thread drew and presented each frame.
    CHECK(count_events(threads, "render", "draw", Kind::begin) >= 1);
    CHECK(count_events(threads, "render", "draw", Kind::end) >= 1);
    CHECK(count_events(threads, "render", "present", Kind::begin) >= 1);
    CHECK(count_events(threads, "render", "present", Kind::end) >= 1);

    // One "logic loop" block per loop turn, not one for the whole run: a run that presented 3
    // frames turned the loop at least three times (the first publish is the one before it).
    CHECK(count_events(threads, "logic", "logic loop", Kind::begin) >= 3);
    CHECK(count_events(threads, "logic", "logic loop", Kind::end) >= 3);

    // Blocks on both threads are paired and properly nested.
    check_blocks_nested(threads, "logic");
    check_blocks_nested(threads, "render");

    // On the render thread, "present" is a sibling of "draw", never nested inside it.
    check_present_not_inside_draw(threads, "render");
}

// FrameStats itself records the "cpu %" and "heap KB" counters on the global recorder, once per
// stats period, with the values its own line reports (the runtime no longer computes them a
// second time). Two stats periods are driven through FrameStats' own API -- its clock is fed
// to tick(), not read from inside.
TEST_CASE("FrameStats records the cpu % and heap KB counters once per stats period")
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    constexpr std::size_t kKB = 1024;
    const auto counters = [](std::uint64_t idle_us) {
        return prism::platform::SystemCounters{
            .idle_us = idle_us,
            .heap_used = 230 * kKB, .heap_total = 420 * kKB,
            .stacks_used = 120 * kKB, .stacks_total = 384 * kKB,
        };
    };
    const auto frame = [](int ms) {
        return prism::platform::StageTimes{std::chrono::milliseconds(ms), std::chrono::milliseconds(ms),
                                           std::chrono::milliseconds(ms), std::chrono::milliseconds(ms)};
    };

    const auto start = Clock::now();
    prism::platform::FrameStats stats{start, 0, counters(1'000'000)};
    stats.add(frame(5));
    const auto line1 = stats.tick(start + 1s, 0, counters(1'250'000)); // 250 ms of the second idle: CPU 75 %
    REQUIRE(line1.has_value());
    const std::string text1{*line1}; // the line lives in the stats' buffer until the next tick
    stats.add(frame(5));
    const auto line2 = stats.tick(start + 2s, 0, counters(1'600'000)); // 350 ms idle: CPU 65 %
    REQUIRE(line2.has_value());
    const std::string text2{*line2};

    // The cpu value each period's line reports, parsed back out of it.
    const auto cpu_in_line = [](const std::string& line) {
        const auto pos = line.find("CPU ");
        REQUIRE(pos != std::string::npos);
        long cpu = -1;
        CHECK(std::sscanf(line.c_str() + pos, "CPU %ld%%", &cpu) == 1);
        return cpu;
    };
    const long cpu1 = cpu_in_line(text1);
    const long cpu2 = cpu_in_line(text2);

    // The counter samples the two ticks left on the global recorder (on this thread's ring;
    // the frame_app case above records none: this binary's system_counters() is nullopt).
    std::vector<std::int32_t> cpu_samples, heap_samples;
    const auto collect = [&](std::span<const Event> events) {
        for (const Event& e : events) {
            if (e.kind != Kind::counter)
                continue;
            if (std::string_view(e.site->name) == "cpu %")
                cpu_samples.push_back(e.value);
            else if (std::string_view(e.site->name) == "heap KB")
                heap_samples.push_back(e.value);
        }
    };
    prism::profile::global().for_each_thread([&](const Recorder::ThreadView& tv) {
        collect(tv.first);
        collect(tv.second);
    });

    // One sample per period, equal to what that period's line reported, and within range.
    REQUIRE(cpu_samples.size() == 2);
    CHECK(cpu_samples[0] == cpu1);
    CHECK(cpu_samples[1] == cpu2);
    for (const std::int32_t v : cpu_samples) {
        CHECK(v >= 0);
        CHECK(v <= 100);
    }
    REQUIRE(heap_samples.size() == 2);
    CHECK(heap_samples[0] == 230); // 230 * 1024 bytes of heap in use, as KB
    CHECK(heap_samples[1] == 230);
}
