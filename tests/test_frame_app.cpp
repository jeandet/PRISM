// frame_app: a model runs on a framebuffer target (here a vector of pixels and a scripted
// touch) through the frame runtime, and returns once the app quits.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_app.hpp>
#include <prism/platform/frame_stats.hpp>

#include <prism/app/widget_tree.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/raster/fonts.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
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
using namespace std::chrono_literals;
using prism::platform::FrameContext;

namespace {

constexpr int W = 120, H = 80;

struct Counter {
    Field<prism::ui::Button> plus{{"+1"}};
    Field<int> count{0};
    void view(prism::app::ViewBuilder& vb) { vb.vstack(plus, count); }
};

struct MemorySink final : prism::platform::FrameSink {
    std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(std::size_t(W) * H);
    std::atomic<int> presents{0};
    std::chrono::milliseconds present_time{0};

    std::span<std::uint32_t> back() override { return pixels; }
    void present() override
    {
        std::this_thread::sleep_for(present_time);
        ++presents;
    }
    Size size() const override { return {Width{float(W)}, Height{float(H)}}; }
};

// No finger, or a tap at `at`: held for a few polls once the first frame is on screen (the
// runtime hit-tests against the shown frame, set just after the first present), then lifted.
struct ScriptedTouch final : prism::platform::TouchSource {
    const MemorySink* sink = nullptr;
    std::optional<Point> at;
    int held_polls = 5;

    std::optional<Point> read() override
    {
        if (!at || sink->presents == 0 || held_polls == 0)
            return std::nullopt;
        --held_polls;
        return at;
    }
};

// The button's centre, laid out as the runtime lays it out: the same model type, size and fonts.
Point button_centre()
{
    Counter twin;
    prism::app::WidgetTree tree(twin);
    prism::raster::Fonts fonts;
    tree.set_text_measurer(&fonts);
    const auto snap = tree.build_snapshot(float(W), float(H), 1);
    const auto button = tree.leaf_ids().front();
    for (const auto& [id, r] : snap->geometry)
        if (id == button)
            return r.center();
    FAIL("no geometry for the button");
    return {};
}

} // namespace

TEST_CASE("frame_app presents a first frame and returns after quit")
{
    Counter model;
    MemorySink sink;
    ScriptedTouch touch;
    touch.sink = &sink;
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        ctx.on_loop([&] {
            if (sink.presents >= 1)
                ctx.quit();
        });
    });
    CHECK(sink.presents >= 1);
}

TEST_CASE("a tap on the button reaches the model on the logic thread")
{
    Counter model;
    MemorySink sink;
    ScriptedTouch touch;
    touch.sink = &sink;
    touch.at = button_centre();
    std::thread::id clicked_on;
    Connection on_click;
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        on_click = model.plus.on_change().connect([&](const prism::ui::Button&) {
            clicked_on = std::this_thread::get_id();
            model.count.set(model.count.get() + 1);
        });
        ctx.on_loop([&] {
            if (model.count.get() == 1)
                ctx.quit();
        });
    });
    CHECK(model.count.get() == 1);
    CHECK(clicked_on != std::thread::id{});
    CHECK(clicked_on != std::this_thread::get_id());
}

TEST_CASE("quit while the render thread waits ends the run")
{
    Counter model;
    MemorySink sink;
    ScriptedTouch touch;
    touch.sink = &sink;
    const auto start = std::chrono::steady_clock::now();
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        ctx.on_loop([&, polls = 0]() mutable {
            if (sink.presents >= 1 && ++polls >= 3) // the render thread is idle by now
                ctx.quit();
        });
    });
    CHECK(sink.presents >= 1);
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("quit while the render thread is inside present() ends the run")
{
    Counter model;
    MemorySink sink;
    sink.present_time = 50ms;
    ScriptedTouch touch;
    touch.sink = &sink;
    const auto start = std::chrono::steady_clock::now();
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        ctx.on_loop([&] { ctx.quit(); });
    });
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("the timing line reaches on_stats")
{
    Counter model;
    MemorySink sink;
    ScriptedTouch touch;
    touch.sink = &sink;
    std::string line;
    prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
        ctx.on_stats([&](std::string_view l) {
            line = l;
            ctx.quit();
        });
    });
    REQUIRE_FALSE(line.empty());
    CHECK(std::isdigit(static_cast<unsigned char>(line.front())));
    CHECK(line.find("fps") != std::string::npos);
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {
template <typename F>
bool aborts(F f)
{
    const pid_t pid = fork();
    if (pid == 0) { f(); _exit(0); }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
} // namespace

TEST_CASE("registering a hook from inside a running hook aborts")
{
    CHECK(aborts([] {
        Counter model;
        MemorySink sink;
        ScriptedTouch touch;
        touch.sink = &sink;
        prism::platform::frame_app(sink, touch, model, [&](FrameContext& ctx) {
            ctx.on_loop([&] { ctx.on_loop([] {}); });
        });
    }));
}
#endif
