// The app asks the platform for files through AppContext; the answer reaches the app on the
// logic thread whichever thread the platform answers on.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/headless_window.hpp>
#include <prism/app/model_app.hpp>
#include <prism/app/platform_services.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace prism;
using namespace prism::app;
using namespace prism::core;
using namespace std::chrono_literals;

namespace {
struct Empty {
    Field<int> x{0};
};

// Answers every dialog from a thread of its own, as a portal or a native dialog does.
struct ForeignThreadServices final : PlatformServices {
    std::vector<std::string> answer;
    FileDialog last;
    std::vector<std::thread> threads;
    std::vector<std::string> messages;
    ~ForeignThreadServices() override { for (auto& t : threads) t.join(); }
    void show_file_dialog(WindowId, FileDialog d, std::function<void(FileDialogResult)> done) override {
        last = d;
        threads.emplace_back([done = std::move(done), paths = answer] { done({paths, {}}); });
    }
    void show_message(WindowId, MessageKind, std::string title, std::string) override {
        messages.push_back(std::move(title));
    }
};

// Runs the app until `script` (on the backend thread) returns, then closes it.
class ScriptBackend final : public BackendBase {
    HeadlessWindow window_{1, {}};

public:
    std::atomic<AppContext*> ctx{nullptr};
    std::function<void(AppContext&)> script;
    PlatformServices* services = nullptr;

    Window& create_window(WindowConfig) override { return window_; }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {}
    void wake() override {}
    void quit() override {}
    PlatformServices* platform_services() override { return services; }
    void run(std::function<void(const WindowEvent&)> send) override {
        while (!ctx.load()) std::this_thread::yield();
        script(*ctx.load());
        send({1, WindowClose{}});
    }
};

void run_app(ScriptBackend* b, std::unique_ptr<ScriptBackend> owner) {
    Empty model;
    Backend backend{std::move(owner)};
    auto& window = backend.create_window({});
    model_app(backend, window, model, [b](AppContext& c) { b->ctx = &c; });
}

// Waits on the backend thread until `flag` is set by the logic thread.
bool wait_for(std::atomic<bool>& flag) {
    auto end = std::chrono::steady_clock::now() + 2s;
    while (!flag && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    return flag;
}
}

TEST_CASE("choose_files: the platform's answer arrives on the logic thread") {
    ForeignThreadServices services;
    services.answer = {"/tmp/a.txt"};
    auto owner = std::make_unique<ScriptBackend>();
    auto* b = owner.get();
    b->services = &services;
    std::atomic<bool> got{false};
    bool on_logic_thread = false;
    std::vector<std::string> paths;
    b->script = [&](AppContext& ctx) {
        ctx.post([&] {
            ctx.choose_files({.kind = FileDialogKind::Open, .title = "Open a note",
                              .filters = {{"Text", "txt;md"}}, .start = {}},
                             [&](FileDialogResult r) {
                                 on_logic_thread = detail_is_logic_thread;
                                 paths = r.paths;
                                 got = true;
                             });
        });
        REQUIRE(wait_for(got));
    };
    run_app(b, std::move(owner));
    CHECK(on_logic_thread);
    CHECK(paths == std::vector<std::string>{"/tmp/a.txt"});
    CHECK(services.last.title == "Open a note");
    CHECK(services.last.filters.size() == 1);
}

TEST_CASE("choose_files without platform services reports an error instead of never answering") {
    auto owner = std::make_unique<ScriptBackend>();
    auto* b = owner.get();
    std::atomic<bool> got{false};
    FileDialogResult result;
    b->script = [&](AppContext& ctx) {
        ctx.post([&] { ctx.choose_files({}, [&](FileDialogResult r) { result = r; got = true; }); });
        REQUIRE(wait_for(got));
    };
    run_app(b, std::move(owner));
    CHECK(result.paths.empty());
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("show_message reaches the platform") {
    ForeignThreadServices services;
    auto owner = std::make_unique<ScriptBackend>();
    auto* b = owner.get();
    b->services = &services;
    std::atomic<bool> sent{false};
    b->script = [&](AppContext& ctx) {
        ctx.post([&] {
            ctx.show_message(MessageKind::Error, "Could not save", "Disk full");
            sent = true;
        });
        REQUIRE(wait_for(sent));
    };
    run_app(b, std::move(owner));
    CHECK(services.messages == std::vector<std::string>{"Could not save"});
}

// --- drag and drop -----------------------------------------------------------------------

namespace {
struct Form {
    Field<TextField<>> name{{}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(name); }
};

// Sends `events` once the first frame exists, then closes.
class DropBackend final : public BackendBase {
    HeadlessWindow window_{1, {.width = 400, .height = 300}};
    std::mutex m_;
    std::condition_variable cv_;
    bool framed_ = false;

public:
    std::vector<InputEvent> events;
    Window& create_window(WindowConfig) override { return window_; }
    void submit(WindowId, std::shared_ptr<const SceneSnapshot>) override {
        { std::lock_guard l(m_); framed_ = true; }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}
    void run(std::function<void(const WindowEvent&)> send) override {
        { std::unique_lock l(m_); cv_.wait_for(l, 2s, [&] { return framed_; }); }
        for (auto& ev : events) send({1, ev});
        send({1, WindowClose{}});
    }
};
}

TEST_CASE("a file drop reaches the app's drop handler on the logic thread") {
    Form model;
    auto impl = std::make_unique<DropBackend>();
    impl->events = {FileDrop{Point{X{10}, Y{10}}, {"/tmp/a.txt", "/tmp/b.txt"}}};
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    std::vector<std::string> got;
    bool on_logic_thread = false;
    model_app(backend, window, model, [&](AppContext& ctx) {
        ctx.on_file_drop([&](const FileDrop& d) {
            got = d.paths;
            on_logic_thread = detail_is_logic_thread;
        });
    });
    CHECK(got == std::vector<std::string>{"/tmp/a.txt", "/tmp/b.txt"});
    CHECK(on_logic_thread);
}

TEST_CASE("text dropped on a text field is typed into it") {
    Form model;
    auto impl = std::make_unique<DropBackend>();
    impl->events = {TextDrop{Point{X{20}, Y{10}}, "dropped"}};
    Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    model_app(backend, window, model);
    CHECK(model.name.get().value == "dropped");
}
