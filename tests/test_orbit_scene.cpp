// The gallery's Orbit game: the solver that proves a scene winnable and the seeded generator that
// only offers such scenes, tested here with no window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit_levels.hpp"
#include "../examples/widget_gallery/orbit_scene.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace orbit;

namespace {
constexpr double step_s = 1.0 / 240.0;   // the fixed step every flight is made of

// Seeds the sweep tries at each difficulty. Every scene costs two full grid searches (the
// generator proves its own scene, then the test proves it again), and the default build is -O0, so
// this is the plan's reduced sweep; raise it once the release timings are known.
constexpr std::uint64_t sweep_seeds = 20;

// Steps until the probe stops flying, or gives up after `max_steps`.
Outcome fly(World& w, const Level& level, double gravity, int max_steps = 100'000) {
    for (int i = 0; i < max_steps; ++i)
        if (auto outcome = step(w, level, gravity, step_s); outcome != Outcome::Flying) return outcome;
    return Outcome::Flying;
}

bool wins(const Level& level, Vec velocity, double gravity = 1.0) {
    World w = launch(level, velocity);
    return fly(w, level, gravity) == Outcome::Hit;
}

bool same_body(const Body& a, const Body& b) {
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.vel.x == b.vel.x && a.vel.y == b.vel.y &&
           a.mass == b.mass && a.radius == b.radius && a.anchor == b.anchor;
}

bool same_level(const Level& a, const Level& b) {
    if (a.start.x != b.start.x || a.start.y != b.start.y) return false;
    if (a.target.x != b.target.x || a.target.y != b.target.y) return false;
    if (a.target_radius != b.target_radius || a.target_body != b.target_body) return false;
    if (a.bodies.size() != b.bodies.size()) return false;
    for (std::size_t i = 0; i < a.bodies.size(); ++i)
        if (!same_body(a.bodies[i], b.bodies[i])) return false;
    return true;
}

bool in_field(Vec p) { return p.x > 0 && p.x < field_width && p.y > 0 && p.y < field_height; }

Vec ring_of(const Level& level) {
    if (!level.target_body || *level.target_body >= level.bodies.size()) return level.target;
    return level.bodies[*level.target_body].pos;
}

// A scene the generator may offer: everything inside the field, nothing on top of the probe's
// start, no two bodies already touching, and the ring clear of every body it does not ride.
void check_scene(const Level& level, std::uint64_t seed, int difficulty) {
    INFO("seed " << seed << ", difficulty " << difficulty);
    CHECK(in_field(level.start));
    CHECK(in_field(level.target));
    for (const auto& body : level.bodies) {
        CHECK(in_field(body.pos));
        CHECK(length(body.pos - level.start) > body.radius);
    }
    for (std::size_t i = 0; i < level.bodies.size(); ++i)
        for (std::size_t j = i + 1; j < level.bodies.size(); ++j)
            CHECK(length(level.bodies[i].pos - level.bodies[j].pos) >=
                  level.bodies[i].radius + level.bodies[j].radius);
    const bool carried = level.target_body && *level.target_body < level.bodies.size();
    const Vec ring = ring_of(level);
    for (std::size_t i = 0; i < level.bodies.size(); ++i) {
        if (carried && i == *level.target_body) continue;
        CHECK(length(level.bodies[i].pos - ring) > level.bodies[i].radius + level.target_radius);
    }
}
} // namespace

TEST_CASE("every hand-made level has a winning launch") {
    for (const auto& level : levels()) {
        const auto velocity = solve(level);
        REQUIRE(velocity.has_value());
        CHECK(wins(level, *velocity));
        CHECK(length(*velocity) >= 0.2 * max_launch_speed - 1e-9);
        CHECK(length(*velocity) <= max_launch_speed + 1e-9);
    }
}

TEST_CASE("the solver honours the gravity it is given") {
    const Level& level = levels().front();
    const auto velocity = solve(level, 2.0);
    REQUIRE(velocity.has_value());
    CHECK(wins(level, *velocity, 2.0));
}

TEST_CASE("a scene the probe cannot escape has no winning launch") {
    Level level{.start = {500, 300}, .target = {500, 575}, .target_radius = 20, .bodies = {}};
    for (int i = 0; i < 12; ++i) {
        const double angle = i * 6.283185307179586 / 12;
        level.bodies.push_back({.pos = {500 + std::cos(angle) * 48, 300 + std::sin(angle) * 48},
                                .mass = 1.0,
                                .radius = 46});
    }
    CHECK_FALSE(solve(level).has_value());
}

TEST_CASE("the same seed gives the same scene") {
    CHECK(same_level(random_level(42, 3), random_level(42, 3)));
}

TEST_CASE("different seeds give different scenes") {
    CHECK_FALSE(same_level(random_level(1, 3), random_level(2, 3)));
}

TEST_CASE("every random scene is winnable at every difficulty") {
    for (int difficulty = 1; difficulty <= 5; ++difficulty)
        for (std::uint64_t seed = 1; seed <= sweep_seeds; ++seed) {
            const Level level = random_level(seed, difficulty);
            check_scene(level, seed, difficulty);
            const auto velocity = solve(level);
            CHECK_MESSAGE(velocity.has_value(), "seed " << seed << ", difficulty " << difficulty);
            if (velocity) CHECK(wins(level, *velocity));
        }
}
