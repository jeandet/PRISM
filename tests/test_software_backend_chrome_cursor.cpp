#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/software_backend.hpp>

#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

namespace {

void push_motion(SDL_WindowID window_id, float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.windowID = window_id;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

// Re-pushes the motion while polling: under a real X server (Xvfb on CI) the pointer's
// own enter/motion events race the synthetic one and can overwrite or coalesce away a
// single push, so one push is not a reliable stimulus.
bool hover_until(prism::backends::SdlWindow& win, SDL_WindowID window_id, float x, float y,
                 prism::CursorShape expected) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline) {
        push_motion(window_id, x, y);
        if (win.cursor() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

TEST_CASE("SoftwareBackend sets a resize cursor when the mouse hovers a custom-chrome window edge") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    // backend.run() drives SDL's window creation and event pump, which on macOS must happen
    // on the process's real main thread (AppKit requirement) -- so it runs here, and the
    // test's own injection/assertions move to a worker thread instead.
    std::thread driver([&] {
        backend.wait_ready();
        auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

        // left edge, well below the title bar
        CHECK(hover_until(sdl_win, window_id, 2.f, 150.f, prism::CursorShape::ResizeEW));

        backend.quit();
    });

    backend.run([](const prism::WindowEvent&) {});
    driver.join();
}

TEST_CASE("SoftwareBackend sets a diagonal resize cursor when the mouse hovers a corner") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::thread driver([&] {
        backend.wait_ready();
        auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

        // top-left corner
        CHECK(hover_until(sdl_win, window_id, 2.f, 2.f, prism::CursorShape::ResizeNWSE));

        backend.quit();
    });

    backend.run([](const prism::WindowEvent&) {});
    driver.join();
}

TEST_CASE("SoftwareBackend forwards MouseMove and leaves the cursor alone when the mouse is over client content") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::atomic<int> mouse_move_count{0};
    std::thread driver([&] {
        backend.wait_ready();
        auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

        // left edge — chrome-owned, not forwarded
        CHECK(hover_until(sdl_win, window_id, 2.f, 150.f, prism::CursorShape::ResizeEW));

        push_motion(window_id, 150.f, 150.f); // client area — forwarded, cursor untouched by chrome
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (mouse_move_count.load(std::memory_order_acquire) == 0 &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

        CHECK(mouse_move_count.load(std::memory_order_acquire) >= 1);
        CHECK(sdl_win.cursor() == prism::CursorShape::ResizeEW); // chrome layer never touched it back

        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) {
        if (std::holds_alternative<prism::MouseMove>(we.event))
            mouse_move_count.fetch_add(1, std::memory_order_release);
    });
    driver.join();
}

TEST_CASE("SoftwareBackend coalesces a burst of queued motion events into far fewer dispatches") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);

    std::atomic<int> mouse_move_count{0};
    std::thread driver([&] {
        backend.wait_ready();
        auto window_id = SDL_GetWindowID(sdl_win.sdl_window());

        // All client-area (y=150, well below the title bar) so every one of these reaches the
        // "forward as MouseMove" path, not the chrome/resize early-outs -- same zone as the test
        // above. Pushed back-to-back from this thread, far faster than the backend thread can
        // wake from SDL_WaitEvent and drain them one at a time, so most queue up before draining
        // starts.
        constexpr int burst = 200;
        for (int i = 0; i < burst; ++i)
            push_motion(window_id, 100.f + static_cast<float>(i), 150.f);

        // Wait for the dispatched count to stop changing (the whole burst has been drained and
        // processed) rather than guessing a fixed sleep duration.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        int last = -1, stable_ticks = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            int now = mouse_move_count.load(std::memory_order_acquire);
            if (now == last) {
                if (++stable_ticks >= 5) break;
            } else {
                stable_ticks = 0;
                last = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        int dispatched = mouse_move_count.load(std::memory_order_acquire);
        CHECK(dispatched >= 1);
        // Coalescing should collapse this burst to a small handful of dispatches, not merely
        // "fewer than the absolute max by chance" -- a loose `< burst` bound passed even against
        // the pre-fix code (194/200 got through with zero coalescing), so it proved nothing.
        CHECK(dispatched < burst / 4);

        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) {
        if (std::holds_alternative<prism::MouseMove>(we.event))
            mouse_move_count.fetch_add(1, std::memory_order_release);
    });
    driver.join();
}

namespace {
// Moves into the content first (a leave needs a prior entry), then pushes `ev`.
bool push_until(std::atomic<bool>& seen, SDL_WindowID window_id, const SDL_Event& ev) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < deadline) {
        push_motion(window_id, 150.f, 150.f);
        SDL_Event copy = ev;
        SDL_PushEvent(&copy);
        if (seen.load()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return seen.load();
}

bool is_mouse_leave(const prism::WindowEvent& we) { return std::holds_alternative<prism::MouseLeave>(we.event); }
}

TEST_CASE("SoftwareBackend reports the pointer leaving the window") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);
    std::atomic<bool> left{false};

    std::thread driver([&] {
        backend.wait_ready();
        SDL_Event ev{};
        ev.type = SDL_EVENT_WINDOW_MOUSE_LEAVE;
        ev.window.windowID = SDL_GetWindowID(sdl_win.sdl_window());
        CHECK(push_until(left, SDL_GetWindowID(sdl_win.sdl_window()), ev));
        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) { if (is_mouse_leave(we)) left = true; });
    driver.join();
}

TEST_CASE("SoftwareBackend: moving onto a custom title bar leaves the content") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);
    std::atomic<bool> left{false};

    std::thread driver([&] {
        backend.wait_ready();
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.windowID = SDL_GetWindowID(sdl_win.sdl_window());
        ev.motion.x = 150.f;
        ev.motion.y = 10.f;   // inside the title bar, away from its buttons
        CHECK(push_until(left, SDL_GetWindowID(sdl_win.sdl_window()), ev));
        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) { if (is_mouse_leave(we)) left = true; });
    driver.join();
}

TEST_CASE("SoftwareBackend: a move queued before going onto the title bar is not delivered after the leave") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300,
                                           .decoration = prism::DecorationMode::Custom});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);
    std::mutex m;
    std::vector<int> kinds;   // 0 move, 1 leave

    std::thread driver([&] {
        backend.wait_ready();
        auto id = SDL_GetWindowID(sdl_win.sdl_window());
        for (int round = 0; round < 20; ++round) {   // back to back: one pump batch, so the move is coalesced
            push_motion(id, 150.f, 150.f);
            push_motion(id, 150.f, 10.f);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) {
        std::lock_guard l(m);
        if (std::holds_alternative<prism::MouseMove>(we.event)) kinds.push_back(0);
        if (is_mouse_leave(we)) kinds.push_back(1);
    });
    driver.join();
    REQUIRE_FALSE(kinds.empty());
    CHECK(kinds.back() == 1);   // the pointer ended on the title bar: the last word is a leave
}

TEST_CASE("SoftwareBackend stamps each event with the frame on screen") {
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300});
    auto& sdl_win = static_cast<prism::backends::SdlWindow&>(window);
    auto frame = std::make_shared<prism::SceneSnapshot>();
    frame->viewport = prism::Size{prism::Width{300}, prism::Height{300}};
    std::atomic<bool> stamped{false};
    std::atomic<bool> seen{false};

    std::thread driver([&] {
        backend.wait_ready();
        backend.submit(window.id(), frame);
        backend.wake();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (!seen && std::chrono::steady_clock::now() < deadline) {
            if (auto st = backend.present_stats(window.id()); st && st->present_count > 0)
                push_motion(SDL_GetWindowID(sdl_win.sdl_window()), 50.f, 50.f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        backend.quit();
    });

    backend.run([&](const prism::WindowEvent& we) {
        if (!std::holds_alternative<prism::MouseMove>(we.event)) return;
        seen = true;
        stamped = we.shown.get() == frame.get();
    });
    driver.join();
    REQUIRE(seen);
    CHECK(stamped);
}

TEST_CASE("SoftwareBackend: a file dialog that cannot open answers with an error, not silence") {
    SDL_SetHint(SDL_HINT_FILE_DIALOG_DRIVER, "no-such-driver");   // never shows a real dialog
    prism::backends::SoftwareBackend backend{{}};
    auto& window = backend.create_window({.width = 300, .height = 300});
    std::atomic<bool> answered{false};
    prism::app::FileDialogResult result;
    std::mutex m;

    std::thread driver([&] {
        backend.wait_ready();
        REQUIRE(backend.platform_services());
        backend.platform_services()->show_file_dialog(
            window.id(), {.kind = prism::app::FileDialogKind::Open, .title = "Open", .filters = {{"Text", "txt"}}, .start = {}},
            [&](prism::app::FileDialogResult r) {
                std::lock_guard l(m);
                result = std::move(r);
                answered = true;
            });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (!answered && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        backend.quit();
    });
    backend.run([](const prism::WindowEvent&) {});
    driver.join();
    SDL_ResetHint(SDL_HINT_FILE_DIALOG_DRIVER);
    REQUIRE(answered);
    std::lock_guard l(m);
    CHECK(result.paths.empty());
    CHECK_FALSE(result.error.empty());
}
