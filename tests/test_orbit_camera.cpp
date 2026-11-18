// The gallery's Orbit camera: the view that keeps the field and the flying probe in frame, and how
// it eases toward it. Plain values, no window and no rendering.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit_camera.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

using namespace orbit;

namespace {
constexpr double margin = 60;        // the field units the view keeps clear around what it shows
constexpr double aspect = 5.0 / 3.0; // the gallery canvas's shape
constexpr double tau = 0.25;         // the camera's easing time constant, in seconds
constexpr double frame_s = 1.0 / 60.0;

double height_of(const View& v, double a = aspect) { return v.width / a; }

// Whether the view shows `p`, with `pad` field units to spare on every side.
bool shows(const View& v, Vec p, double pad = 0) {
    return std::abs(p.x - v.centre.x) <= v.width / 2 - pad + 1e-9 &&
           std::abs(p.y - v.centre.y) <= height_of(v) / 2 - pad + 1e-9;
}

// The field's four corners, margin included, all in view.
void check_field_shown(const View& v) {
    const Vec corners[] = {{0, 0}, {field_width, 0}, {0, field_height}, {field_width, field_height}};
    for (Vec corner : corners) CHECK(shows(v, corner, margin));
}

double lost_reach_units() { return lost_reach * field_diagonal(); }

// The view's whole rectangle inside the lost box: 1.5 diagonals around the field's centre.
bool inside_lost_box(const View& v, double a = aspect) {
    const Vec c = field_centre();
    const double reach = lost_reach_units();
    const double half = v.width / 2, half_h = height_of(v, a) / 2;
    return v.centre.x - half >= c.x - reach - 1e-9 && v.centre.x + half <= c.x + reach + 1e-9 &&
           v.centre.y - half_h >= c.y - reach - 1e-9 && v.centre.y + half_h <= c.y + reach + 1e-9;
}

// The smallest width a view of this aspect can have and still show the field plus its margin.
double field_only_width(double a) {
    return std::max(field_width + 2 * margin, (field_height + 2 * margin) * a);
}
} // namespace

TEST_CASE("with no probe the view is the field and its margin") {
    const View v = wanted_view(std::nullopt, aspect);
    check_field_shown(v);
    CHECK(v.width == doctest::Approx(field_only_width(aspect)));
    CHECK(v.width == doctest::Approx(1200.0)); // 720 tall at 5/3, wider than the 1120 the margin asks
    CHECK(height_of(v) == doctest::Approx(field_height + 2 * margin));
    CHECK(v.centre.x == doctest::Approx(field_centre().x));
    CHECK(v.centre.y == doctest::Approx(field_centre().y));
}

TEST_CASE("a probe off to the right widens the view and the view follows it") {
    const View bare = wanted_view(std::nullopt, aspect);
    const Vec probe{1300, 300};
    const View v = wanted_view(probe, aspect);
    CHECK(v.width > bare.width);
    CHECK(v.centre.x > bare.centre.x);
    CHECK(shows(v, probe, margin));
    check_field_shown(v);
    CHECK(v.width == doctest::Approx(1420.0)); // the field's left margin to 60 past the probe
    CHECK(height_of(v) == doctest::Approx(1420.0 / aspect));
}

TEST_CASE("a probe inside the field does not move the camera") {
    const View bare = wanted_view(std::nullopt, aspect);
    for (Vec inside : {Vec{500, 300}, Vec{700, 450}, Vec{120, 560}}) {
        const View v = wanted_view(inside, aspect);
        CHECK(v.width == bare.width);
        CHECK(v.centre.x == bare.centre.x);
        CHECK(v.centre.y == bare.centre.y);
    }
}

TEST_CASE("the view is the smallest one of the canvas's aspect that holds the field and its margin") {
    for (double a : {0.5, 1.0, 2.0}) {
        const View v = wanted_view(std::nullopt, a);
        CHECK(v.width == doctest::Approx(field_only_width(a)));
        CHECK(v.width / a >= field_height + 2 * margin); // tall enough for the margin too
        CHECK(v.centre.x == doctest::Approx(field_centre().x));
        CHECK(v.centre.y == doctest::Approx(field_centre().y));
    }
}

TEST_CASE("the camera never shows past the lost box") {
    const Vec c = field_centre();
    const double reach = lost_reach_units();
    const double cap = 2 * reach;
    const Vec near_corner{c.x + 0.99 * reach, c.y + 0.99 * reach};
    for (Vec probe : {Vec{c.x + 10 * reach, c.y}, Vec{c.x - 4 * reach, c.y - 3 * reach}, near_corner}) {
        const View v = wanted_view(probe, aspect);
        CHECK(v.width <= cap + 1e-9);
        CHECK(inside_lost_box(v));
    }
    // A probe far past the box does not make the view grow past it either.
    const View v = wanted_view(Vec{c.x + 10 * reach, c.y}, aspect);
    CHECK(v.width == doctest::Approx(cap));
}

TEST_CASE("a portrait canvas keeps the view inside the lost box too") {
    const double portrait = 0.5;
    const Vec c = field_centre();
    const double reach = lost_reach_units();
    const View v = wanted_view(Vec{c.x + 10 * reach, c.y}, portrait);
    CHECK(v.width == doctest::Approx(2 * reach * portrait)); // the box's height is the tighter limit
    CHECK(v.width / portrait <= 2 * reach + 1e-9);
    CHECK(v.centre.y == doctest::Approx(c.y)); // exactly the box's height, so centred on it
    CHECK(inside_lost_box(v, portrait));
}

TEST_CASE("easing closes half the gap in 0.25 ln2 seconds") {
    const View now{.centre = {0, 0}, .width = 1000};
    const View wanted{.centre = {100, 200}, .width = 500};
    const View half = eased(now, wanted, tau * std::log(2.0));
    CHECK(half.centre.x == doctest::Approx(50.0));
    CHECK(half.centre.y == doctest::Approx(100.0));
    CHECK(half.width == doctest::Approx(750.0));
}

TEST_CASE("no time eases nothing, and twenty time constants arrive") {
    const View now{.centre = {0, 0}, .width = 1000};
    const View wanted{.centre = {100, 200}, .width = 500};
    const View still = eased(now, wanted, 0.0);
    CHECK(still.centre.x == now.centre.x);
    CHECK(still.centre.y == now.centre.y);
    CHECK(still.width == now.width);

    const View settled = eased(now, wanted, 20 * tau);
    CHECK(settled.centre.x == doctest::Approx(wanted.centre.x).epsilon(1e-6));
    CHECK(settled.centre.y == doctest::Approx(wanted.centre.y).epsilon(1e-6));
    CHECK(settled.width == doctest::Approx(wanted.width).epsilon(1e-6));
}

TEST_CASE("easing never overshoots") {
    const View now{.centre = {0, 0}, .width = 1000};
    const View wanted{.centre = {100, 200}, .width = 500};
    for (int frame = 1; frame <= 10; ++frame) {
        const View v = eased(now, wanted, frame * 0.05);
        CHECK(v.width <= now.width);
        CHECK(v.width >= wanted.width);
        CHECK(v.centre.x >= now.centre.x);
        CHECK(v.centre.x <= wanted.centre.x);
        CHECK(v.centre.y >= now.centre.y);
        CHECK(v.centre.y <= wanted.centre.y);
    }
}

TEST_CASE("the camera follows a flight and comes back once it ends") {
    const View bare = wanted_view(std::nullopt, aspect);
    // The rock sits past the field's right edge, so the camera has to widen before the crash.
    Level level{.start = {900, 300}, .target = {200, 300}, .target_radius = 20};
    level.bodies.push_back(Body{.pos = {1400, 300}, .mass = 1e-6, .radius = 40});

    World w = launch(level, {500, 0});
    double carry = 0;
    View view = wanted_view(w.probe.pos, aspect);
    double widest = view.width;
    Outcome outcome = Outcome::Flying;
    for (int frame = 0; frame < 600 && outcome == Outcome::Flying; ++frame) {
        outcome = advance(w, level, 1.0, frame_s, carry);
        if (outcome == Outcome::Flying) view = eased(view, wanted_view(w.probe.pos, aspect), frame_s);
        CHECK(inside_lost_box(view));
        widest = std::max(widest, view.width);
    }
    CHECK(outcome == Outcome::Crashed);
    CHECK(widest > bare.width);
    CHECK(view.centre.x > bare.centre.x);

    for (int frame = 0; frame < 600; ++frame) view = eased(view, bare, frame_s);
    CHECK(view.width == doctest::Approx(bare.width).epsilon(1e-4));
    CHECK(view.centre.x == doctest::Approx(bare.centre.x).epsilon(1e-4));
    CHECK(view.centre.y == doctest::Approx(bare.centre.y).epsilon(1e-4));
}
