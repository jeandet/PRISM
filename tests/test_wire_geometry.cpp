#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/wire_geometry.hpp>

using namespace prism::nodes;
using prism::core::Point;
using prism::core::X;
using prism::core::Y;

namespace {
Point pt(float x, float y) { return Point{X{x}, Y{y}}; }
}

TEST_CASE("a wire leaves and enters horizontally") {
    auto w = wire_curve(pt(0, 0), pt(200, 80));
    CHECK(w.c0.y == w.p0.y);
    CHECK(w.c1.y == w.p1.y);
    CHECK(w.c0.x.raw() > w.p0.x.raw());
    CHECK(w.c1.x.raw() < w.p1.x.raw());
}

TEST_CASE("its ends are its points") {
    auto w = wire_curve(pt(10, 20), pt(200, 80));
    CHECK(point_on(w, 0) == pt(10, 20));
    CHECK(point_on(w, 1) == pt(200, 80));
}

TEST_CASE("a point on the wire is at distance 0, one 10 above a straight wire at 10") {
    auto w = wire_curve(pt(0, 0), pt(100, 0));
    CHECK(distance_to(w, pt(50, 0)) < 0.5f);
    CHECK(distance_to(w, pt(50, -10)) == doctest::Approx(10).epsilon(0.05));
}

TEST_CASE("a backward wire still curves out of its port") {
    auto w = wire_curve(pt(100, 0), pt(0, 50));
    CHECK(w.c0.x.raw() >= 140.f);
}
