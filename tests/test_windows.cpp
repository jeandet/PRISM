// Several app windows: AppContext::open_window gives a second model its own window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/headless_window.hpp>
#include <prism/app/model_app.hpp>

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>

using namespace prism;
using namespace prism::app;
using namespace prism::core;
using namespace prism::ui;
using namespace std::chrono_literals;

namespace {
struct Main {
    Field<TextField<>> text{{.value = "hello"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(text); }
};

// A second window's model, reading the first's field.
struct Echo {
    Derived<std::string> copy;
    explicit Echo(Main& m) : copy([&m] { return "echo: " + m.text.get().value; }, m.text) {}
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(copy); }
};

// Hands out windows 1, 2, ...; records what reaches each; runs `script` once the first
// frame is up, then closes the main window.
class Windows final : public BackendBase {
    std::map<WindowId, std::unique_ptr<HeadlessWindow>> windows_;
    WindowId next_ = 1;
    std::mutex m_;
    std::condition_variable cv_;

public:
    std::map<WindowId, std::shared_ptr<const SceneSnapshot>> last;
    std::vector<WindowId> closed;
    std::vector<std::function<void(std::function<void(const WindowEvent&)>&)>> script;

    Window& create_window(WindowConfig cfg) override {
        std::lock_guard l(m_);
        auto id = next_++;
        return *(windows_[id] = std::make_unique<HeadlessWindow>(id, cfg));
    }
    void close_window(WindowId id) override { std::lock_guard l(m_); closed.push_back(id); }
    void submit(WindowId id, std::shared_ptr<const SceneSnapshot> snap) override {
        { std::lock_guard l(m_); last[id] = std::move(snap); }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}
    bool wait_for(auto pred) {
        std::unique_lock l(m_);
        return cv_.wait_for(l, 2s, [&] { return pred(); });
    }
    void run(std::function<void(const WindowEvent&)> send) override {
        wait_for([&] { return last.contains(1); });
        for (auto& step : script) step(send);
        send({1, WindowClose{}});
    }
};

bool shows(const SceneSnapshot* snap, std::string_view text) {
    if (!snap) return false;
    for (auto& dl : snap->draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<render::TextCmd>(&c); t && t->text == text) return true;
    return false;
}
}

TEST_CASE("a second window shows its own model, and follows the first one's fields") {
    Main main;
    Echo echo{main};
    auto impl = std::make_unique<Windows>();
    auto* b = impl.get();
    WindowId second = 0;
    AppContext* ctx = nullptr;
    b->script.push_back([&](auto&) {
        REQUIRE(b->wait_for([&] { return b->last.contains(2); }));
        CHECK(shows(b->last[2].get(), "echo: hello"));
    });
    b->script.push_back([&](auto&) {
        ctx->post([&] { main.text.set({.value = "world"}); });
        CHECK(b->wait_for([&] { return shows(b->last[2].get(), "echo: world"); }));
    });
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, main, [&](AppContext& c) {
        ctx = &c;
        second = c.open_window(echo, {.title = "Echo", .width = 300, .height = 100});
    });
    CHECK(second == 2);
}

TEST_CASE("closing a second window removes it; the app goes on") {
    Main main;
    Echo echo{main};
    auto impl = std::make_unique<Windows>();
    auto* b = impl.get();
    AppContext* ctx = nullptr;
    WindowId second = 0;
    bool open_after = true;
    b->script.push_back([&](auto& send) {
        REQUIRE(b->wait_for([&] { return b->last.contains(2); }));
        send({2, WindowClose{}});
        std::atomic<bool> done{false};
        ctx->post([&] { open_after = ctx->window_open(second); done = true; });
        auto end = std::chrono::steady_clock::now() + 2s;
        while (!done && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    });
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, main, [&](AppContext& c) {
        ctx = &c;
        second = c.open_window(echo, {.title = "Echo"});
    });
    CHECK_FALSE(open_after);
    CHECK(b->closed == std::vector<WindowId>{2});
}

TEST_CASE("a new window takes the app's theme") {
    Main main;
    Echo echo{main};
    auto impl = std::make_unique<Windows>();
    auto* b = impl.get();
    b->script.push_back([&](auto&) {
        REQUIRE(b->wait_for([&] { return b->last.contains(2); }));
        CHECK(b->last[2]->window.background == light_theme().canvas_bg);
    });
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, main, [&](AppContext& c) {
        c.set_theme(light_theme());
        c.open_window(echo, {.title = "Echo"});
    });
}
