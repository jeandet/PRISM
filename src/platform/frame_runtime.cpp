// The frame runtime (see platform/frame_app.hpp). Threading model, mirroring model_app: one
// thread owns the model and the WidgetTree and is the only one that sets fields; the other only
// renders snapshots handed over through a mutex and a fixed set of snapshot slots (see Handoff).
// Input goes through PRISM's public path, app::widget_detail::route_event()
// (app/event_routing.hpp), hit-tested against the last *shown* snapshot -- the same `shown`
// model_app's dispatch uses. Both threads stop on the run's stop_source (FrameContext::quit).
#include <prism/platform/frame_app.hpp>

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/core/logic_thread.hpp>
#include <prism/input/input_event.hpp>
#include <prism/raster/canvas.hpp>
#include <prism/raster/draw_snapshot.hpp>
#include <prism/raster/fonts.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <thread>

namespace prism::platform {

namespace platform_detail {
struct ContextAccess {
    static void loop(const FrameContext& c)
    {
        for (const auto& fn : c.loop_)
            fn();
    }
    static void stats(const FrameContext& c, std::string_view line)
    {
        for (const auto& fn : c.stats_)
            fn(line);
    }
};
} // namespace platform_detail

namespace {

using platform_detail::ContextAccess;

constexpr auto kPollPeriod = std::chrono::milliseconds(16); // ~60 Hz touch polling
// pthread stacks: the logic thread builds snapshots (prism-stm32's QEMU tests used a 64 KiB
// task stack for that), the render thread rasterizes into the framebuffer.
constexpr std::size_t kLogicStackBytes = 64 * 1024;
constexpr std::size_t kRenderStackBytes = 48 * 1024;

// The handoff between the two threads, over a fixed set of snapshot slots that are rebuilt in
// place (WidgetTree::build_snapshot into a slot keeps its capacity: no allocation per frame).
// A slot is `pending` (built, waiting to be drawn), `drawing` (the render thread reads it) or
// `shown` (on screen: input is hit-tested against it); the logic thread builds into a slot that
// is none of the three, so four slots always leave one free. Only the logic thread writes
// slots, and only a free one; indices are -1 when unset and change under `mu`.
struct Handoff {
    static constexpr int kSlots = 4;
    std::mutex mu;
    std::condition_variable_any cv; // _any: its wait wakes on the stop token
    std::array<render::SceneSnapshot, kSlots> slots;
    int pending = -1, drawing = -1, shown = -1;
    std::chrono::microseconds pending_update_time{}; // the loop hooks before `pending`
    std::chrono::microseconds pending_build_time{};  // how long `pending` took to build
    // The timing summary: the render thread adds each finished frame, the logic thread reads
    // a line once a second. Fixed-size, so neither side allocates per frame.
    FrameStats stats{std::chrono::steady_clock::now(), allocation_count(), system_counters()};
};

using Clock = std::chrono::steady_clock;

std::chrono::microseconds since(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start);
}

using app::WidgetTree;
using core::Point;
using input::MouseButton;
using input::MouseMove;
using render::SceneSnapshot;

// One touch poll as PRISM pointer events, in window coordinates, against the shown frame.
void route_touch(WidgetTree& tree, Handoff& h, std::optional<Point> now, std::optional<Point>& last)
{
    int shown;
    {
        std::lock_guard lock{h.mu};
        shown = h.shown;
    }
    if (shown < 0)
        return;
    // Read without the lock: only this (the logic) thread writes slots, and never `shown`'s.
    const SceneSnapshot* snap = &h.slots[std::size_t(shown)];
    const auto moved = [](Point a, Point b) { return a.x.raw() != b.x.raw() || a.y.raw() != b.y.raw(); };
    if (now && !last)
        app::widget_detail::route_event(tree, snap, MouseButton{*now, input::buttons::left, true});
    else if (!now && last)
        app::widget_detail::route_event(tree, snap, MouseButton{*last, input::buttons::left, false});
    else if (now && last && moved(*now, *last))
        app::widget_detail::route_event(tree, snap, MouseMove{*now, input::buttons::left_mask});
    last = now;
}

// A slot that is neither pending, being drawn nor shown (call with h.mu held).
int free_slot(const Handoff& h)
{
    for (int i = 0; i < Handoff::kSlots; ++i)
        if (i != h.pending && i != h.drawing && i != h.shown)
            return i;
    return -1; // unreachable: three indices cannot cover four slots
}

// Builds a snapshot of the tree and hands it to the render thread, with the time the loop
// hooks took and the build time.
void publish(WidgetTree& tree, Handoff& h, core::Size size, std::uint64_t version,
             std::chrono::microseconds update_time)
{
    int slot;
    {
        std::lock_guard lock{h.mu};
        slot = free_slot(h);
    }
    const auto start = Clock::now();
    tree.build_snapshot(h.slots[std::size_t(slot)], size.w.raw(), size.h.raw(), version);
    tree.clear_dirty();
    const auto build_time = since(start);
    {
        std::lock_guard lock{h.mu};
        h.pending = slot;
        h.pending_update_time = update_time;
        h.pending_build_time = build_time;
    }
    h.cv.notify_one();
}

// True while the render thread has not taken the last snapshot yet. Building another one then
// would only replace it unseen: wasted time and allocations. The tree stays dirty, so a later
// loop builds the snapshot once the render thread is ready.
bool snapshot_waiting(Handoff& h)
{
    std::lock_guard lock{h.mu};
    return h.pending >= 0;
}

// Startup is over once the first timing line is out (a second of frames: caches and buffers
// are warm). From then on the guard, when built in, stops on any allocation.
void arm_allocation_guard()
{
#ifdef PRISM_ALLOC_GUARD
    prism_allocation_guard_arm();
#endif
}

// Hands the timing line to the app once a second (the render thread fed the frames into h.stats).
void report_stats(Handoff& h, const FrameContext& ctx)
{
    std::optional<std::string_view> line; // in h.stats' buffer: shown before the next tick
    const std::optional<SystemCounters> system = system_counters(); // takes malloc's lock: not under h.mu
    {
        std::lock_guard lock{h.mu};
        line = h.stats.tick(Clock::now(), allocation_count(), system);
    }
    if (line) {
        ContextAccess::stats(ctx, *line);
        arm_allocation_guard();
    }
}

struct RunArgs {
    Handoff* h;
    FrameSink* sink;
    TouchSource* touch;
    raster::Fonts* fonts;
    core::Size size;
    std::stop_source stop;
    TreeFactory* make_tree;
    std::function<void(FrameContext&)>* setup;
};

void logic_loop(const RunArgs& a, WidgetTree& tree, const FrameContext& ctx)
{
    // First frame, handed to the render thread like any other: it draws and presents it
    // (and records it as `shown`, which input is hit-tested against).
    std::uint64_t version = 0;
    publish(tree, *a.h, a.size, ++version, {});

    const std::stop_token stop = a.stop.get_token();
    std::optional<Point> last_touch;
    while (!stop.stop_requested()) {
        route_touch(tree, *a.h, a.touch->read(), last_touch);
        const auto update_start = Clock::now();
        ContextAccess::loop(ctx);
        const auto update_time = since(update_start);
        report_stats(*a.h, ctx);
        if (tree.any_dirty() && !snapshot_waiting(*a.h))
            publish(tree, *a.h, a.size, ++version, update_time);
        std::this_thread::sleep_for(kPollPeriod);
    }
}

// The model and the tree belong to this thread while the run lasts; the tree goes with it.
void logic_main(const RunArgs& a)
{
    core::logic_thread.store(std::this_thread::get_id());
    {
        std::unique_ptr<WidgetTree> tree = (*a.make_tree)();
        tree->set_text_measurer(a.fonts); // layout measures with the fonts the rasterizer draws with
        FrameContext ctx{a.stop};
        if (*a.setup)
            (*a.setup)(ctx);
        logic_loop(a, *tree, ctx);
    }
    core::logic_thread.store(std::thread::id{}); // the model is the caller's again
}

void render_main(const RunArgs& a)
{
    Handoff& h = *a.h;
    const int width = int(a.size.w.raw()), height = int(a.size.h.raw());
    // One canvas for the whole run, pointed at each frame's back buffer: it keeps its scratch
    // storage, so drawing does not allocate after the first frame.
    raster::Canvas canvas{a.sink->back(), width, height, width};
    const std::stop_token stop = a.stop.get_token();
    for (;;) {
        StageTimes times;
        {
            std::unique_lock lock{h.mu};
            if (!h.cv.wait(lock, stop, [&] { return h.pending >= 0; }))
                return; // quit
            h.drawing = h.pending;
            h.pending = -1;
            times.update = h.pending_update_time;
            times.snapshot = h.pending_build_time;
        }
        const SceneSnapshot* snap = &h.slots[std::size_t(h.drawing)];
        canvas.retarget(a.sink->back());
        auto start = Clock::now();
        canvas.clear(snap->window.background); // clearing is the caller's job, as with SDL
        raster::draw(canvas, *snap, *a.fonts);
        times.draw = since(start);
        start = Clock::now();
        a.sink->present();
        times.present = since(start);
        {
            std::lock_guard lock{h.mu};
            h.shown = h.drawing;
            h.drawing = -1;
            h.stats.add(times);
        }
    }
}

void* logic_entry(void* p)
{
    logic_main(*static_cast<const RunArgs*>(p));
    return nullptr;
}

void* render_entry(void* p)
{
    render_main(*static_cast<const RunArgs*>(p));
    return nullptr;
}

std::optional<pthread_t> start_thread(void* (*entry)(void*), RunArgs& args, std::size_t stack_bytes)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_bytes);
    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, entry, &args);
    pthread_attr_destroy(&attr);
    if (rc != 0)
        return std::nullopt;
    return thread;
}

} // namespace

void run_frames(FrameSink& sink, TouchSource& touch, TreeFactory make_tree,
                std::function<void(FrameContext&)> setup)
{
    // raster::Fonts is the shared text measurer: measure() runs on the logic thread, glyph
    // rasterization on the render thread; the faces are immutable and the glyph cache is
    // mutex-guarded (raster/fonts.hpp).
    raster::Fonts fonts;
    auto h = std::make_unique<Handoff>(); // the snapshot slots are large for a task stack
    RunArgs args{h.get(), &sink, &touch, &fonts, sink.size(), {}, &make_tree, &setup};

    // pthreads, not std::thread: the stack sizes matter on an RTOS, whose default is small.
    const auto logic = start_thread(logic_entry, args, kLogicStackBytes);
    if (!logic)
        return; // no memory for the thread
    const auto render = start_thread(render_entry, args, kRenderStackBytes);
    if (!render)
        args.stop.request_stop();
    pthread_join(*logic, nullptr);
    if (render)
        pthread_join(*render, nullptr);
}

} // namespace prism::platform
