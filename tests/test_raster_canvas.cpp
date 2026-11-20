#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/raster/canvas.hpp>

#include <prism/render/draw_list.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef NDEBUG
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace prism::core;
using prism::render::Image;

namespace {

constexpr std::uint32_t kCleared = 0xFF000000u;   // opaque black: "untouched"

template <int W, int H>
struct Fb {
    std::array<std::uint32_t, W * H> px{};
    prism::raster::Canvas canvas{px, W, H, W};
    Fb() { canvas.clear(Color::rgba(0, 0, 0)); }
    std::uint32_t at(int x, int y) const { return px[static_cast<std::size_t>(y) * W + x]; }
    bool touched(int x, int y) const { return at(x, y) != kCleared; }
    int count_touched() const {
        int n = 0;
        for (std::uint32_t p : px) if (p != kCleared) ++n;
        return n;
    }
};

Rect rect(float x, float y, float w, float h) {
    return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}};
}
Point pt(float x, float y) { return Point{X{x}, Y{y}}; }

} // namespace

TEST_CASE("fill_rect writes exactly its pixels (corners in, one past out)") {
    Fb<16, 16> fb;
    fb.canvas.fill_rect(rect(2, 3, 4, 5), Color::rgba(255, 0, 0));
    CHECK(fb.at(2, 3) == 0xFFFF0000u);   // top-left corner in
    CHECK(fb.at(5, 7) == 0xFFFF0000u);   // bottom-right corner in
    CHECK_FALSE(fb.touched(6, 3));       // one past the right edge out
    CHECK_FALSE(fb.touched(2, 8));       // one past the bottom edge out
    CHECK_FALSE(fb.touched(1, 3));
    CHECK(fb.count_touched() == 4 * 5);
}

TEST_CASE("the clip rect limits fill_rect, pop restores it") {
    Fb<16, 16> fb;
    fb.canvas.push_clip(rect(2, 2, 6, 6));
    fb.canvas.fill_rect(rect(0, 0, 16, 16), Color::rgba(255, 255, 255));
    CHECK(fb.touched(2, 2));
    CHECK(fb.touched(7, 7));
    CHECK_FALSE(fb.touched(1, 5));   // left of the clip
    CHECK_FALSE(fb.touched(8, 5));   // right of the clip ([2, 8) x [2, 8))
    CHECK(fb.count_touched() == 36);
    fb.canvas.pop_clip();
    fb.canvas.fill_rect(rect(0, 0, 1, 1), Color::rgba(0, 255, 0));
    CHECK(fb.touched(0, 0));
}

TEST_CASE("50% alpha over white blends to the expected value") {
    Fb<8, 8> fb;
    fb.canvas.clear(Color::rgba(255, 255, 255));
    fb.canvas.fill_rect(rect(0, 0, 4, 4), Color::rgba(0, 0, 0, 128));
    const std::uint32_t px = fb.at(1, 1);
    const int r = static_cast<int>((px >> 16) & 0xFF);
    const int g = static_cast<int>((px >> 8) & 0xFF);
    const int b = static_cast<int>(px & 0xFF);
    // 255 * (1 - 128/255) = 127.0
    CHECK(std::abs(r - 127) <= 1);
    CHECK(std::abs(g - 127) <= 1);
    CHECK(std::abs(b - 127) <= 1);
    CHECK((px >> 24) == 255);   // alpha stays opaque over an opaque destination
}

TEST_CASE("rounded_rect leaves the corner pixel outside the radius untouched") {
    Fb<16, 16> fb;
    fb.canvas.rounded_rect(rect(2, 2, 12, 12), Color::rgba(255, 255, 255), 4.f, 0.f);
    CHECK_FALSE(fb.touched(2, 2));   // centre (2.5, 2.5) is ~4.9 from the corner centre (6, 6) > 4
    CHECK(fb.touched(6, 2));         // top edge, past the corner arc
    CHECK(fb.touched(2, 6));         // left edge, past the corner arc
    CHECK(fb.touched(7, 7));         // middle
    CHECK(fb.count_touched() > 100); // most of the 12x12 box
}

TEST_CASE("a thickness-1 line from (0,0) to (9,0) covers 10 pixels") {
    Fb<16, 16> fb;
    fb.canvas.line(pt(0, 0), pt(9, 0), Color::rgba(255, 255, 255), 1.f);
    for (int x = 0; x <= 9; ++x) CHECK(fb.touched(x, 0));
    CHECK(fb.count_touched() == 10);
    CHECK_FALSE(fb.touched(0, 1));
}

TEST_CASE("filled circle: centre pixel set, outside not") {
    Fb<16, 16> fb;
    fb.canvas.circle(pt(8, 8), 5.f, Color::rgba(255, 255, 255), 0.f);
    CHECK(fb.touched(8, 8));
    CHECK(fb.touched(12, 8));    // centre (12.5, 8.5): dist ~4.5 <= 5
    CHECK_FALSE(fb.touched(14, 8));   // dist 6.5 > 5
    CHECK_FALSE(fb.touched(0, 0));
    CHECK(fb.count_touched() > 60);   // ~pi * 25 = 78
}

TEST_CASE("filled_polygon fills a triangle within 10% of its analytic area") {
    Fb<24, 24> fb;
    const Point tri[3] = {pt(0, 0), pt(20, 0), pt(0, 20)};
    fb.canvas.filled_polygon(tri, Color::rgba(255, 255, 255));
    const int n = fb.count_touched();
    CHECK(std::abs(n - 200) <= 20);   // area = 20 * 20 / 2 = 200
}

TEST_CASE("image scaled 2x maps texels (nearest neighbour)") {
    const std::vector<std::uint8_t> rgba = {
        255, 0, 0, 255,   0, 255, 0, 255,     // red, green
        0, 0, 255, 255,   255, 255, 0, 255,   // blue, yellow
    };
    auto img = Image::from_rgba(2, 2, rgba);
    REQUIRE(img.has_value());
    Fb<8, 8> fb;
    fb.canvas.image(**img, rect(0, 0, 4, 4), 255, Color::rgba(255, 255, 255));
    CHECK(fb.at(0, 0) == 0xFFFF0000u);
    CHECK(fb.at(1, 1) == 0xFFFF0000u);   // the 2x2 block of the top-left texel
    CHECK(fb.at(2, 0) == 0xFF00FF00u);
    CHECK(fb.at(3, 1) == 0xFF00FF00u);
    CHECK(fb.at(0, 2) == 0xFF0000FFu);
    CHECK(fb.at(3, 3) == 0xFFFFFF00u);
    CHECK_FALSE(fb.touched(4, 0));   // outside the dest rect
}

#ifndef NDEBUG
// The bounds check is an assert, so the child is a fork whose death is inspected with waitpid.
namespace {
template <typename F>
bool aborts(F f) {
    const pid_t pid = fork();
    if (pid == 0) { f(); _exit(0); }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
} // namespace

TEST_CASE("Canvas rejects a pixel span smaller than stride * height") {
    std::array<std::uint32_t, 4 * 4 - 1> small{};
    CHECK(aborts([&] { prism::raster::Canvas c{small, 4, 4, 4}; }));
    std::array<std::uint32_t, 4 * 4> ok{};
    CHECK_FALSE(aborts([&] { prism::raster::Canvas c{ok, 4, 4, 4}; }));
}

TEST_CASE("Canvas::retarget rejects a pixel span smaller than stride * height") {
    std::array<std::uint32_t, 4 * 4> ok{};
    std::array<std::uint32_t, 4 * 4 - 1> small{};
    prism::raster::Canvas c{ok, 4, 4, 4};
    CHECK(aborts([&] { c.retarget(small); }));
}
#endif
