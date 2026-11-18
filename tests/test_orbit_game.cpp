// The gallery's Orbit game rules that need no window: stars, the aim preview, and how persistence
// fades and trims what stays drawn.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit_game.hpp"
#include "../examples/widget_gallery/orbit_levels.hpp"

using namespace orbit;

TEST_CASE("stars: three for one launch, two for up to three, one otherwise") {
    CHECK(stars_for(1) == 3);
    CHECK(stars_for(2) == 2);
    CHECK(stars_for(3) == 2);
    CHECK(stars_for(4) == 1);
    CHECK(stars_for(40) == 1);
}

TEST_CASE("the aim preview is the first 1.5 s of the flight the launch would make") {
    const Level& level = levels().front();
    const Vec velocity{300, -200};
    const auto path = preview_path(level, velocity, 1.0);
    REQUIRE(path.size() > 2);
    CHECK(path.front().x == level.start.x);
    CHECK(path.front().y == level.start.y);

    World w = launch(level, velocity);
    for (int i = 0; i < 360; ++i) CHECK(step(w, level, 1.0, fixed_step) == Outcome::Flying);
    CHECK(path.back().x == w.probe.pos.x);
    CHECK(path.back().y == w.probe.pos.y);
}

TEST_CASE("the aim preview ends where the flight does") {
    const Level& level = levels().front();
    const auto path = preview_path(level, {520, 0}, 1.0);   // straight into the planet
    CHECK(length(path.back() - levels().front().bodies[0].pos) < levels().front().bodies[0].radius);
}

TEST_CASE("fading runs linearly from full for the newest to 15 % for the oldest") {
    CHECK(fade(0, 1) == doctest::Approx(1.0));
    CHECK(fade(0, 5) == doctest::Approx(1.0));
    CHECK(fade(4, 5) == doctest::Approx(0.15));
    CHECK(fade(2, 5) == doctest::Approx(0.575));
}

TEST_CASE("a moving body's trail keeps 2 s x (1 + persistence / 5) of its path") {
    CHECK(trail_seconds(0) == doctest::Approx(2.0));
    CHECK(trail_seconds(5) == doctest::Approx(4.0));
    CHECK(trail_seconds(20) == doctest::Approx(10.0));

    const Level& level = levels()[3];   // has a moon
    World w = launch(level, {0, 0});
    BodyTrails trails;
    for (int i = 0; i < 1200; ++i) {   // 5 s
        step(w, level, 1.0, fixed_step);
        record_trails(trails, w, 2.0);
    }
    REQUIRE(trails.size() == w.bodies.size());
    CHECK(trails[0].empty());   // an anchor leaves no trail
    CHECK(trails[1].front().time >= w.time - 2.0 - fixed_step);
    CHECK(trails[1].back().time == doctest::Approx(w.time));
}

TEST_CASE("a trail's bands: the count is the number of bands drawn, so the newest keeps full colour") {
    for (std::size_t band : {2, 3, 7})
        for (std::size_t size = 2; size <= 60; ++size) {
            std::size_t drawn = 0;
            for (std::size_t from = 0; from + 1 < size; from += band) ++drawn;
            CHECK_MESSAGE(trail_band_count(size, band) == drawn, "size " << size << " band " << band);
        }
}
