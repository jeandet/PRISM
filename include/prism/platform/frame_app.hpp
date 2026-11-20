#pragma once

// frame_app(sink, touch, model): runs a model on a framebuffer target, as model_app runs one in
// an SDL window. Two threads: a logic thread that owns the model and its WidgetTree, polls the
// touch at ~60 Hz and routes it through the tree's input path, and a render thread that draws
// each new snapshot into the sink and presents it. Each frame is timed and summarized once a
// second (platform/frame_stats.hpp) to the app's on_stats hooks. Returns after quit().

#include <prism/app/widget_tree.hpp>
#include <prism/platform/frame_target.hpp>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace prism::platform {

namespace platform_detail {
struct ContextAccess;

// Always on, like the canvas's span check: a hook list that grows while it is called is UB.
inline void require_not_started(bool started)
{
    if (!started)
        return;
    std::fputs("prism::platform::FrameContext: hook registered after setup returned\n", stderr);
    std::abort();
}
} // namespace platform_detail

// The app's handle on the runtime, given to setup on the logic thread.
class FrameContext {
public:
    explicit FrameContext(std::stop_source stop) : stop_(std::move(stop)) {}

    // Runs each logic loop, after input and before the dirty check (e.g. drain a sample source).
    // Register from setup: registering later aborts, as the hooks are being called.
    void on_loop(std::function<void()> fn)
    {
        platform_detail::require_not_started(started_);
        loop_.push_back(std::move(fn));
    }
    // Receives the timing line once a second, on the logic thread. Register from setup.
    void on_stats(std::function<void(std::string_view)> fn)
    {
        platform_detail::require_not_started(started_);
        stats_.push_back(std::move(fn));
    }
    // Ends the run: frame_app returns once both threads are done. Any thread, while the run lasts.
    void quit() { stop_.request_stop(); }

private:
    friend struct platform_detail::ContextAccess;
    std::stop_source stop_;
    bool started_ = false; // set once setup has returned: the hook lists are then fixed
    std::vector<std::function<void()>> loop_;
    std::vector<std::function<void(std::string_view)>> stats_;
};

// Makes the app's WidgetTree; called on the logic thread, which then owns the tree and model.
using TreeFactory = std::function<std::unique_ptr<app::WidgetTree>()>;

void run_frames(FrameSink& sink, TouchSource& touch, TreeFactory make_tree,
                std::function<void(FrameContext&)> setup = {});

// The model must outlive the call; while it runs, only the logic thread may change it.
template <class Model>
void frame_app(FrameSink& sink, TouchSource& touch, Model& model,
               std::function<void(FrameContext&)> setup = {})
{
    run_frames(sink, touch, [&model] { return std::make_unique<app::WidgetTree>(model); },
               std::move(setup));
}

} // namespace prism::platform
