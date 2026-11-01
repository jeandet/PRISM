#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/tessellate.hpp>

using namespace prism::render;
using namespace prism::core;

namespace {
Rect R(float x, float y, float w, float h) { return {Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
Point P(float x, float y) { return {X{x}, Y{y}}; }
const Color red = Color::rgba(255, 0, 0);

SceneSnapshot one_list(DrawList dl) {
    SceneSnapshot s;
    s.draw_lists.push_back(std::make_shared<const DrawList>(std::move(dl)));
    s.z_order.push_back(0);
    return s;
}
}

TEST_CASE("a filled rect is two triangles in one unclipped batch") {
    DrawList dl;
    dl.filled_rect(R(10, 20, 30, 40), red);
    auto t = tessellate(one_list(std::move(dl)));
    REQUIRE(t.batches.size() == 1);
    CHECK_FALSE(t.batches[0].clip);
    CHECK(t.batches[0].count == 6);
    CHECK(t.vertices.size() == 6);
    CHECK(t.vertices[0].r == 1.f);
    CHECK(t.vertices[0].a == 1.f);
    float minx = 1e9, maxy = -1e9;
    for (auto& v : t.vertices) { minx = std::min(minx, v.x); maxy = std::max(maxy, v.y); }
    CHECK(minx == 10.f);
    CHECK(maxy == 60.f);
}

TEST_CASE("clips split batches; a push with nothing before it reuses the empty batch") {
    DrawList dl;
    dl.clip_push(P(0, 0), Size{Width{50}, Height{50}});
    dl.filled_rect(R(0, 0, 10, 10), red);
    dl.clip_pop();
    dl.filled_rect(R(60, 0, 10, 10), red);
    auto t = tessellate(one_list(std::move(dl)));
    REQUIRE(t.batches.size() == 2);
    REQUIRE(t.batches[0].clip);
    CHECK(t.batches[0].clip->extent.w.raw() == 50.f);
    CHECK(t.batches[0].first == 0);
    CHECK(t.batches[0].count == 6);
    CHECK_FALSE(t.batches[1].clip);
    CHECK(t.batches[1].first == 6);
    CHECK(t.batches[1].count == 6);
}

TEST_CASE("the overlay comes last and is never clipped, even after an unbalanced push") {
    DrawList dl;
    dl.clip_push(P(0, 0), Size{Width{5}, Height{5}});
    dl.filled_rect(R(0, 0, 10, 10), red);
    auto s = one_list(std::move(dl));
    s.overlay.filled_rect(R(100, 100, 10, 10), Color::rgba(0, 0, 255));
    auto t = tessellate(s);
    REQUIRE(t.batches.size() == 2);
    CHECK_FALSE(t.batches.back().clip);
    CHECK(t.vertices.back().b == 1.f);
}

TEST_CASE("rounded rects and circles come from the shared meshes, with an alpha fringe") {
    DrawList dl;
    dl.rounded_rect(R(0, 0, 40, 20), red, 6.f);
    dl.circle(P(50, 50), 10.f, red);
    auto t = tessellate(one_list(std::move(dl)));
    CHECK(t.vertices.size() > 12);
    CHECK(t.vertices.size() % 3 == 0);
    bool fringe = false;
    for (auto& v : t.vertices) fringe |= v.a == 0.f;
    CHECK(fringe);
}

TEST_CASE("lines, polylines and strips become triangles") {
    DrawList dl;
    dl.line(P(0, 0), P(10, 0), red, 2.f);
    dl.polyline({P(0, 0), P(10, 0), P(10, 10)}, red, 1.f);
    dl.filled_polygon({P(0, 0), P(10, 0), P(0, 10), P(10, 10)}, red);
    auto t = tessellate(one_list(std::move(dl)));
    CHECK(t.vertices.size() == 6 + 2 * 6 + 2 * 3);
}

TEST_CASE("a line is as thick as asked, centred on its path") {
    DrawList dl;
    dl.line(P(0, 10), P(20, 10), red, 4.f);
    auto t = tessellate(one_list(std::move(dl)));
    float lo = 1e9, hi = -1e9;
    for (auto& v : t.vertices) { lo = std::min(lo, v.y); hi = std::max(hi, v.y); }
    CHECK(lo == doctest::Approx(8.f));
    CHECK(hi == doctest::Approx(12.f));
}

TEST_CASE("text and images are counted, not drawn, in this pass") {
    DrawList dl;
    dl.text("hello", P(0, 0), 14.f, red);
    auto t = tessellate(one_list(std::move(dl)));
    CHECK(t.skipped == 1);
    CHECK(t.vertices.empty());
}
