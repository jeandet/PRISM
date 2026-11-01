// The WebGPU renderer, offscreen: real GPU work (or Mesa's software Vulkan in CI), pixels read back.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/wgpu_renderer.hpp>

#include <array>

using namespace prism::render;
using namespace prism::core;
using prism::backends::WgpuContext;
using prism::backends::render_to_pixels;

namespace {
Rect R(float x, float y, float w, float h) { return {Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
const Color red = Color::rgba(255, 0, 0), blue = Color::rgba(0, 0, 255), black = Color::rgba(0, 0, 0);

struct Image {
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

Image draw(DrawList dl, int w, int h, float scale = 1.f, Color clear = black) {
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
