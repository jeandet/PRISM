// The gallery's Orbit game: its physics is plain functions over plain values, tested here with
// no window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit.hpp"

#include <cmath>
#include <vector>

using namespace orbit;

namespace {
// Steps until the probe stops flying, or gives up after `max_steps`.
Outcome fly(Probe& p, const Level& level, double gravity, int max_steps = 100'000) {
    for (int i = 0; i < max_steps; ++i)
        if (auto o = step(p, level, gravity, 1.0 / 240.0); o != Outcome::Flying) return o;
    return Outcome::Flying;
}
Level empty_level() { return {.start = {100, 300}, .target = {900, 300}, .target_radius = 30, .planets = {}}; }
} // namespace

TEST_CASE("with no planet a probe flies straight") {
    Probe p{.pos = {100, 300}, .vel = {200, 0}};
    auto level = empty_level();
    level.target = {500, 100};   // out of its way
    step(p, level, 1.0, 0.5);
    CHECK(p.pos.x == doctest::Approx(200));
    CHECK(p.pos.y == doctest::Approx(300));
}

TEST_CASE("a probe aimed at the target with no planet reaches it") {
    Probe p{.pos = {100, 300}, .vel = {400, 0}};
    CHECK(fly(p, empty_level(), 1.0) == Outcome::Hit);
}

TEST_CASE("a probe that leaves the field is lost") {
    Probe p{.pos = {100, 300}, .vel = {-400, 0}};
    CHECK(fly(p, empty_level(), 1.0) == Outcome::Lost);
}

TEST_CASE("a probe aimed straight at a planet crashes into it") {
    Level level = empty_level();
    level.planets = {{.pos = {500, 300}, .mass = 1.0, .radius = 40}};
    Probe p{.pos = {100, 300}, .vel = {300, 0}};
    CHECK(fly(p, level, 1.0) == Outcome::Crashed);
}

TEST_CASE("a planet pulls a passing probe toward it") {
    Level level = empty_level();
    level.planets = {{.pos = {500, 200}, .mass = 1.0, .radius = 20}};
    level.target = {500, 900};   // out of reach
    Probe p{.pos = {300, 300}, .vel = {300, 0}};
    for (int i = 0; i < 240; ++i) step(p, level, 1.0, 1.0 / 240.0);
    CHECK(p.vel.y < 0);   // bent up, toward the planet above
}

TEST_CASE("stronger gravity bends the path more") {
    Level level = empty_level();
    level.planets = {{.pos = {500, 200}, .mass = 1.0, .radius = 20}};
    level.target = {500, 900};
    Probe weak{.pos = {300, 300}, .vel = {300, 0}}, strong = weak;
    for (int i = 0; i < 240; ++i) {
        step(weak, level, 0.5, 1.0 / 240.0);
        step(strong, level, 2.0, 1.0 / 240.0);
    }
    CHECK(strong.vel.y < weak.vel.y);
}

TEST_CASE("a launch is the drag, reversed and scaled, capped at the top speed") {
    Vec v = launch_velocity({100, 300}, {60, 330});   // pulled back down-left
    CHECK(v.x > 0);
    CHECK(v.y < 0);
    Vec fast = launch_velocity({100, 300}, {-5000, 300});
    CHECK(std::hypot(fast.x, fast.y) == doctest::Approx(max_launch_speed));
}

TEST_CASE("every level can be won: its target is not inside a planet, and in the field") {
    for (const auto& level : levels()) {
        CHECK(level.target.x > 0);
        CHECK(level.target.x < field_width);
        for (const auto& planet : level.planets) {
            double d = std::hypot(planet.pos.x - level.target.x, planet.pos.y - level.target.y);
            CHECK(d > planet.radius + level.target_radius);
        }
    }
}

TEST_CASE("every level has a winning launch at the default gravity") {
    for (const auto& level : levels()) {
        bool won = false;
        for (int a = 0; a < 360 && !won; a += 2)
            for (double pull = 20; pull <= 180 && !won; pull += 10) {
                double angle = a * 3.14159265358979 / 180;
                Vec release = level.start + Vec{std::cos(angle), std::sin(angle)} * pull;
                Probe p{.pos = level.start, .vel = launch_velocity(level.start, release)};
                won = fly(p, level, 1.0, 240 * 20) == Outcome::Hit;
            }
        CHECK(won);
    }
}

TEST_CASE("many short ticks fly as far as the time they add up to, not one step each") {
    auto level = empty_level();
    level.target = {500, 100};   // out of its way
    Probe p{.pos = {100, 300}, .vel = {200, 0}};
    double carry = 0;
    for (int i = 0; i < 100; ++i) advance(p, level, 1.0, 0.001, carry);   // 0.1 s in all
    CHECK(p.pos.x == doctest::Approx(120).epsilon(0.01));   // 200 units/s for 0.1 s
}
