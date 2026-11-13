// model_app publishes once for a burst of queued input events, not once per event
// (doc/review-2026-11-13.md, P0 #1).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/backend.hpp>
#include <prism/app/headless_window.hpp>
#include <prism/app/model_app.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

using namespace prism;
using namespace prism::app;
using namespace prism::input;
using namespace std::chrono_literals;

namespace {
struct Form {
    core::Field<ui::Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(ok); }
};

// Sends its burst of events in one go, then lets the app's setup go on, so the whole burst is
// queued on the logic thread before the first event of it is handled. Closes once a frame
// came after the burst.
class BurstBackend final : public BackendBase {
    HeadlessWindow window_{1, {.width = 400, .height = 300}};
    std::mutex m_;
    std::condition_variable cv_;
    bool queued_ = false;
    std::size_t frames_ = 0;

public:
    std::vector<InputEvent> burst;
    std::size_t frames_before_burst = 0;

    Window& create_window(WindowConfig) override { return window_; }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {
        { std::lock_guard l(m_); ++frames_; }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}
    void run(std::function<void(const WindowEvent&)> send) override {
        for (auto& ev : burst) send({1, ev});
        { std::lock_guard l(m_); queued_ = true; }
        cv_.notify_all();
        {
            std::unique_lock l(m_);
            cv_.wait_for(l, 2s, [&] { return frames_ > frames_before_burst; });
        }
        send({1, WindowClose{}});
    }
    // Called from the app's setup, on the logic thread: holds it until the burst is queued.
    void wait_for_burst() {
        std::unique_lock l(m_);
        cv_.wait_for(l, 2s, [&] { return queued_; });
        frames_before_burst = frames_;
    }
    std::size_t frames() {
        std::lock_guard l(m_);
        return frames_;
    }
};
}

TEST_CASE("a burst of clicks is drawn once, with all of them applied") {
    Form model;
    auto impl = std::make_unique<BurstBackend>();
    auto* b = impl.get();
    for (int i = 0; i < 10; ++i) {
        b->burst.push_back(MouseButton{Point{X{10}, Y{10}}, buttons::left, true});
        b->burst.push_back(MouseButton{Point{X{10}, Y{10}}, buttons::left, false});
    }
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, model, [&](AppContext&) { b->wait_for_burst(); });
    CHECK(model.ok.get().click_count == 10);
    CHECK(b->frames() - b->frames_before_burst == 1);
}
