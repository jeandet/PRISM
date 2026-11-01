// The WebGPU renderer, offscreen: real GPU work (or Mesa's software Vulkan in CI), pixels read back.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/wgpu_renderer.hpp>

#include <array>
#include <cmath>
#include <optional>

using namespace prism::render;
using namespace prism::core;
using prism::backends::WgpuContext;
using prism::backends::render_to_pixels;
using prism::render::FontRole;
using prism::render::TextAnchor;

namespace {
Rect R(float x, float y, float w, float h) { return {Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
const Color red = Color::rgba(255, 0, 0), blue = Color::rgba(0, 0, 255), black = Color::rgba(0, 0, 0);

struct Pixels {
    int w, h;
    std::vector<std::uint8_t> px;
    std::array<int, 4> at(int x, int y) const {
        auto* p = &px[(static_cast<size_t>(y) * w + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }
};

WgpuContext* context() {
    static auto ctx = WgpuContext::create();
    return ctx.get();
}

SceneSnapshot scene(DrawList dl) {
    SceneSnapshot s;
    s.draw_lists.push_back(std::make_shared<const DrawList>(std::move(dl)));
    s.z_order.push_back(0);
    return s;
}

Pixels draw(DrawList dl, int w, int h, float scale = 1.f, Color clear = black) {
    return {w, h, render_to_pixels(*context(), scene(std::move(dl)), w, h, scale, clear)};
}

// This test exists only in -Dwebgpu=enabled builds: no adapter is a failure, not a skip, so a
// CI lane cannot pass without drawing anything. (Mesa's lavapipe gives one without a GPU.)
bool skip_without_gpu() {
    REQUIRE_MESSAGE(context(), "no WebGPU adapter: install a Vulkan driver (mesa-vulkan-drivers gives lavapipe)");
    return false;
}
}

TEST_CASE("an empty scene is the clear colour") {
    if (skip_without_gpu()) return;
    auto img = draw(DrawList{}, 32, 32, 1.f, blue);
    REQUIRE(img.px.size() == 32u * 32 * 4);
    CHECK(img.at(0, 0) == std::array{0, 0, 255, 255});
    CHECK(img.at(31, 31) == std::array{0, 0, 255, 255});
}

TEST_CASE("a filled rect lands on its pixels, at 1x and 2x") {
    if (skip_without_gpu()) return;
    DrawList dl;
    dl.filled_rect(R(10, 10, 20, 20), red);
    auto one = draw(dl, 64, 64, 1.f);
    CHECK(one.at(15, 15) == std::array{255, 0, 0, 255});
    CHECK(one.at(9, 15) == std::array{0, 0, 0, 255});
    CHECK(one.at(29, 29) == std::array{255, 0, 0, 255});
    CHECK(one.at(30, 30) == std::array{0, 0, 0, 255});
    auto two = draw(dl, 128, 128, 2.f);
    CHECK(two.at(21, 21) == std::array{255, 0, 0, 255});
    CHECK(two.at(19, 21) == std::array{0, 0, 0, 255});
    CHECK(two.at(59, 59) == std::array{255, 0, 0, 255});
    CHECK(two.at(60, 60) == std::array{0, 0, 0, 255});
}

TEST_CASE("a clip cuts what is drawn inside it") {
    if (skip_without_gpu()) return;
    DrawList dl;
    dl.clip_push(Point{X{0}, Y{0}}, Size{Width{16}, Height{64}});
    dl.filled_rect(R(0, 0, 64, 64), red);
    dl.clip_pop();
    auto img = draw(std::move(dl), 64, 64);
    CHECK(img.at(8, 8) == std::array{255, 0, 0, 255});
    CHECK(img.at(40, 8) == std::array{0, 0, 0, 255});
}

TEST_CASE("translucent colours blend over what is below") {
    if (skip_without_gpu()) return;
    DrawList dl;
    dl.filled_rect(R(0, 0, 32, 32), blue);
    dl.filled_rect(R(0, 0, 32, 32), Color::rgba(255, 0, 0, 128));
    auto img = draw(std::move(dl), 32, 32);
    auto p = img.at(16, 16);
    CHECK(p[0] == doctest::Approx(128).epsilon(0.02));
    CHECK(p[2] == doctest::Approx(127).epsilon(0.02));
}

TEST_CASE("a rounded rect leaves its corners empty and softens its edge") {
    if (skip_without_gpu()) return;
    DrawList dl;
    dl.rounded_rect(R(4, 4, 40, 40), red, 12.f);
    auto img = draw(std::move(dl), 48, 48);
    CHECK(img.at(5, 5) == std::array{0, 0, 0, 255});     // inside the box, outside the corner arc
    CHECK(img.at(24, 24) == std::array{255, 0, 0, 255});
    bool partial = false;   // some edge pixel is between the two: the anti-aliasing fringe
    for (int x = 0; x < 48; ++x) {
        int r = img.at(x, 24)[0];
        partial |= r > 10 && r < 245;
    }
    CHECK(partial);
}

// ---- Images and text ----

namespace {
// A stand-in for a font: every string is a solid white box, 2 em wide and 1 em tall.
struct BoxFont {
    int calls = 0;
    std::vector<float> sizes;
    prism::backends::TextRasterizer fn() {
        return [this](std::string_view, float px, FontRole) {
            ++calls;
            sizes.push_back(px);
            int w = static_cast<int>(std::lround(2 * px)), h = static_cast<int>(std::lround(px));
            return std::optional{prism::backends::TextBitmap{w, h, std::vector<std::uint8_t>(static_cast<size_t>(w) * h * 4, 255)}};
        };
    }
};

Pixels draw_text(DrawList dl, int w, int h, float scale, BoxFont& font) {
    return {w, h, render_to_pixels(*context(), scene(std::move(dl)), w, h, scale, black, font.fn())};
}
const Color green = Color::rgba(0, 255, 0);
}

TEST_CASE("an image is drawn scaled into its rect") {
    if (skip_without_gpu()) return;
    auto img = prism::render::Image::from_rgba(2, 2, {255, 0, 0, 255,   0, 255, 0, 255,
                                                      0, 0, 255, 255,   255, 255, 255, 255});
    REQUIRE(img);
    DrawList dl;
    dl.image(*img, R(8, 8, 32, 32));
    auto out = draw(std::move(dl), 48, 48);
    // Each texel fills a 16 x 16 quarter. Scaling filters linearly (as SDL does), so a pixel
    // near a texel's centre carries a few percent of its neighbours.
    auto near = [](std::array<int, 4> got, std::array<int, 4> want) {
        for (int i = 0; i < 4; ++i)
            if (std::abs(got[i] - want[i]) > 24) return false;
        return true;
    };
    CHECK(near(out.at(16, 16), {255, 0, 0, 255}));
    CHECK(near(out.at(31, 16), {0, 255, 0, 255}));
    CHECK(near(out.at(16, 31), {0, 0, 255, 255}));
    CHECK(near(out.at(31, 31), {255, 255, 255, 255}));
    CHECK(out.at(4, 4) == std::array{0, 0, 0, 255});
}

TEST_CASE("text is rasterized at size x scale, placed at its origin and tinted") {
    if (skip_without_gpu()) return;
    for (float scale : {1.f, 2.f}) {
        BoxFont font;
        DrawList dl;
        dl.text("hi", Point{X{5}, Y{5}}, 10.f, green);   // a 20 x 10 point box
        int s = static_cast<int>(scale);
        auto out = draw_text(std::move(dl), 40 * s, 30 * s, scale, font);
        INFO("scale " << scale);
        REQUIRE(font.sizes.size() == 1);
        CHECK(font.sizes[0] == doctest::Approx(10.f * scale));
        CHECK(out.at(6 * s, 6 * s) == std::array{0, 255, 0, 255});
        CHECK(out.at(24 * s, 14 * s) == std::array{0, 255, 0, 255});
        CHECK(out.at(26 * s, 6 * s) == std::array{0, 0, 0, 255});
        CHECK(out.at(6 * s, 16 * s) == std::array{0, 0, 0, 255});
    }
}

TEST_CASE("centred text turned 90 degrees stands upright around its centre") {
    if (skip_without_gpu()) return;
    BoxFont font;
    DrawList dl;
    dl.text("axis", Point{X{30}, Y{30}}, 10.f, green, 90.f, TextAnchor::Center);   // 20 x 10, turned
    auto out = draw_text(std::move(dl), 60, 60, 1.f, font);
    CHECK(out.at(30, 22) == std::array{0, 255, 0, 255});   // 10 wide, 20 tall now
    CHECK(out.at(30, 38) == std::array{0, 255, 0, 255});
    CHECK(out.at(38, 30) == std::array{0, 0, 0, 255});
    CHECK(out.at(22, 30) == std::array{0, 0, 0, 255});
}

TEST_CASE("text is cut by its clip, and the same string is rasterized once per frame") {
    if (skip_without_gpu()) return;
    BoxFont font;
    DrawList dl;
    dl.clip_push(Point{X{0}, Y{0}}, Size{Width{12}, Height{40}});
    dl.text("same", Point{X{0}, Y{0}}, 10.f, green);
    dl.clip_pop();
    dl.text("same", Point{X{0}, Y{20}}, 10.f, green);
    auto out = draw_text(std::move(dl), 40, 40, 1.f, font);
    CHECK(font.calls == 1);
    CHECK(out.at(6, 5) == std::array{0, 255, 0, 255});
    CHECK(out.at(16, 5) == std::array{0, 0, 0, 255});    // past the clip
    CHECK(out.at(16, 25) == std::array{0, 255, 0, 255});  // the second one is not clipped
}

// ---- Layers: content below a title bar, the bar on top ----

TEST_CASE("a layer starting lower draws in its own band, clips included, at 1x and 2x") {
    if (skip_without_gpu()) return;
    for (float scale : {1.f, 2.f}) {
        int s = static_cast<int>(scale);
        DrawList content;
        content.clip_push(Point{X{0}, Y{0}}, Size{Width{20}, Height{100}});
        content.filled_rect(R(0, 0, 40, 10), red);   // the clip keeps its left 20 points
        content.clip_pop();
        auto snap = scene(std::move(content));
        DrawList bar;
        bar.filled_rect(R(0, 0, 40, 8), blue);
        prism::backends::Layer layers[] = {{&snap, nullptr, 8.f}, {nullptr, &bar, 0.f}};
        Pixels out{40 * s, 40 * s,
                  render_to_pixels(*context(), std::span{layers}, 40 * s, 40 * s, scale, black)};
        INFO("scale " << scale);
        CHECK(out.at(5 * s, 4 * s) == std::array{0, 0, 255, 255});   // the bar, on top
        CHECK(out.at(5 * s, 12 * s) == std::array{255, 0, 0, 255});  // content, moved down 8 points
        CHECK(out.at(30 * s, 12 * s) == std::array{0, 0, 0, 255});   // past the content's clip
        CHECK(out.at(5 * s, 20 * s) == std::array{0, 0, 0, 255});    // below the 10-point rect
    }
}

// ---- From the opencode review of 7162853 ----

TEST_CASE("a frame of text only draws, and a string the font cannot render draws nothing") {
    if (skip_without_gpu()) return;
    BoxFont font;
    DrawList dl;
    dl.text("only", Point{X{2}, Y{2}}, 10.f, green);
    auto out = draw_text(std::move(dl), 30, 20, 1.f, font);
    CHECK(out.at(5, 5) == std::array{0, 255, 0, 255});

    prism::backends::TextRasterizer none = [](std::string_view, float, FontRole) { return std::optional<prism::backends::TextBitmap>{}; };
    DrawList dl2;
    dl2.text("missing", Point{X{2}, Y{2}}, 10.f, green);
    auto empty = render_to_pixels(*context(), scene(std::move(dl2)), 30, 20, 1.f, black, none);
    REQUIRE(empty.size() == 30u * 20 * 4);
    CHECK(empty[(5 * 30 + 5) * 4 + 1] == 0);
}

TEST_CASE("a zero scale is treated as 1, not turned into NaN quads") {
    if (skip_without_gpu()) return;
    BoxFont font;
    DrawList dl;
    dl.text("t", Point{X{2}, Y{2}}, 10.f, green);
    auto out = render_to_pixels(*context(), scene(std::move(dl)), 30, 20, 0.f, black, font.fn());
    REQUIRE(out.size() == 30u * 20 * 4);
    CHECK(out[(5 * 30 + 5) * 4 + 1] == 255);
}
