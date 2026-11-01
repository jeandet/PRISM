// An SDL window drawn with WebGPU. Needs a display with native surfaces (X11 or Wayland: CI runs
// it under Xvfb on Mesa's lavapipe); on SDL's offscreen driver it checks the fallback instead.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_font_set.hpp>
#include <prism/backends/sdl_window.hpp>
#include <prism/backends/wgpu_renderer.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstring>
#include <memory>

using namespace prism;
using namespace prism::render;
using namespace prism::core;

namespace {
struct Session {
    explicit Session(const char* driver) {
        if (driver) SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, driver, SDL_HINT_OVERRIDE);   // beats SDL_VIDEODRIVER
        ok = SDL_Init(SDL_INIT_VIDEO) && TTF_Init();
        if (ok) fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f);
    }
    ~Session() {
        fonts.close();
        TTF_Quit();
        SDL_Quit();
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    }
    bool native() const {
        const char* d = SDL_GetCurrentVideoDriver();
        return d && (std::strcmp(d, "x11") == 0 || std::strcmp(d, "wayland") == 0);
    }
    bool ok = false;
    backends::SdlFontSet fonts;
};

SceneSnapshot sample() {
    DrawList dl;
    dl.filled_rect(Rect{Point{X{10}, Y{10}}, Size{Width{60}, Height{30}}}, Color::rgba(200, 40, 40));
    dl.text("WebGPU", Point{X{12}, Y{50}}, 14.f, Color::rgba(230, 230, 230));
    dl.rounded_rect(Rect{Point{X{90}, Y{10}}, Size{Width{60}, Height{30}}}, Color::rgba(40, 105, 180), 8.f);
    SceneSnapshot s;
    s.draw_lists.push_back(std::make_shared<const DrawList>(std::move(dl)));
    s.z_order.push_back(0);
    return s;
}
}

TEST_CASE("a window draws with WebGPU on a real display, through a resize, without errors") {
    Session session{nullptr};
    if (!session.ok || !session.native()) {
        MESSAGE("no X11/Wayland display here: covered in CI under Xvfb");
        return;
    }
    auto ctx = backends::WgpuContext::create();
    REQUIRE(ctx);
    backends::SdlWindow win{1, app::WindowConfig{.title = "webgpu", .width = 240, .height = 140,
                                                 .decoration = app::DecorationMode::Custom}};
    win.use_webgpu(ctx.get());
    win.ensure_created();
    REQUIRE(win.drawing_with_webgpu());
    CHECK(win.renderer() == nullptr);

    auto snap = sample();
    for (int i = 0; i < 3; ++i) win.render_snapshot(snap, session.fonts);
    win.set_size(320, 200);
    SDL_SyncWindow(win.sdl_window());
    for (int i = 0; i < 3; ++i) win.render_snapshot(snap, session.fonts);

    CHECK(win.webgpu_frames() >= 4);   // a frame may be skipped while the surface is reconfigured
    CHECK(ctx->error_count() == 0);
}

TEST_CASE("without a native surface, a window asked for WebGPU falls back to SDL_Renderer") {
    Session session{"offscreen"};
    REQUIRE(session.ok);
    auto ctx = backends::WgpuContext::create();
    REQUIRE(ctx);
    backends::SdlWindow win{1, app::WindowConfig{.title = "fallback", .width = 120, .height = 80,
                                                 .decoration = app::DecorationMode::None}};
    win.use_webgpu(ctx.get());
    win.ensure_created();
    CHECK_FALSE(win.drawing_with_webgpu());
    CHECK(win.renderer() != nullptr);
    auto snap = sample();
    win.render_snapshot(snap, session.fonts);   // and draws as before
}
