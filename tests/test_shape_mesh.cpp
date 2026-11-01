#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/shape_mesh.hpp>

#include <algorithm>
#include <cmath>

using namespace prism::render;
using prism::core::Point;
using prism::core::Rect;

namespace {
Rect R(float x, float y, float w, float h) {
    return {Point{prism::core::X{x}, prism::core::Y{y}}, prism::core::Size{prism::core::Width{w}, prism::core::Height{h}}};
}
Point P(float x, float y) { return {prism::core::X{x}, prism::core::Y{y}}; }
float dist(float x0, float y0, float x1, float y1) { return std::hypot(x1 - x0, y1 - y0); }
}

TEST_CASE("circle outline points lie on the circle") {
    auto pts = circle_outline(P(10.f, 20.f), 5.f, 16);
    REQUIRE(pts.size() == 16);
    for (auto& p : pts) CHECK(dist(10.f, 20.f, p.x, p.y) == doctest::Approx(5.f));
}

TEST_CASE("rounded rect outline stays inside the rect and touches all four sides") {
    auto pts = rounded_rect_outline(R(0, 0, 100, 40), 8.f);
    auto [minx, maxx] = std::ranges::minmax(pts, {}, &Vec2::x);
    auto [miny, maxy] = std::ranges::minmax(pts, {}, &Vec2::y);
    CHECK(minx.x == doctest::Approx(0.f));
    CHECK(maxx.x == doctest::Approx(100.f));
    CHECK(miny.y == doctest::Approx(0.f));
    CHECK(maxy.y == doctest::Approx(40.f));
    // The corner is rounded: no outline point at the exact corner.
    for (auto& p : pts) CHECK_FALSE((p.x < 0.5f && p.y < 0.5f));
}

TEST_CASE("the radius is clamped to half the shorter side") {
    auto pts = rounded_rect_outline(R(0, 0, 100, 10), 50.f);   // a pill: radius becomes 5
    for (auto& p : pts) CHECK(p.y >= -0.001f);
    for (auto& p : pts) CHECK(p.y <= 10.001f);
}

TEST_CASE("a filled shape is opaque inside with a transparent 1 px fringe outside") {
    auto pts = circle_outline(P(0.f, 0.f), 10.f, 32);
    auto mesh = fill_convex(pts);
    int opaque = 0, fringe = 0;
    for (auto& v : mesh.vertices) {
        if (v.alpha == 1.f) ++opaque;
        if (v.alpha == 0.f) {
            ++fringe;
            CHECK(std::hypot(v.x, v.y) == doctest::Approx(11.f).epsilon(0.02));
        }
    }
    CHECK(fringe == 32);
    CHECK(opaque == 33);   // centre + outline
    CHECK(mesh.indices.size() == 32 * 3 + 32 * 6);   // fan + fringe quads
    CHECK(std::ranges::max(mesh.indices) < static_cast<int>(mesh.vertices.size()));
}

TEST_CASE("a stroke spans from the outline inward by its thickness") {
    auto pts = circle_outline(P(0.f, 0.f), 10.f, 32);
    auto mesh = stroke_closed(pts, 2.f);
    float min_r = 1e9f, max_r = 0.f;
    for (auto& v : mesh.vertices) {
        if (v.alpha != 1.f) continue;
        min_r = std::min(min_r, std::hypot(v.x, v.y));
        max_r = std::max(max_r, std::hypot(v.x, v.y));
    }
    CHECK(max_r == doctest::Approx(10.f).epsilon(0.01));
    CHECK(min_r == doctest::Approx(8.f).epsilon(0.02));
}

TEST_CASE("segment count grows with the radius, within bounds") {
    CHECK(arc_segments(1.f) >= 2);
    CHECK(arc_segments(100.f) > arc_segments(4.f));
    CHECK(arc_segments(10000.f) <= 64);
}
