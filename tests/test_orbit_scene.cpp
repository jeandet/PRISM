// The gallery's Orbit game: the solver that proves a scene winnable and the seeded generator that
// only offers such scenes, tested here with no window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit_levels.hpp"
#include "../examples/widget_gallery/orbit_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

using namespace orbit;

namespace {
constexpr double step_s = 1.0 / 240.0;   // the fixed step every flight is made of

// Seeds the sweep tries at each difficulty: the plan's 1-100 in a release build, ten in a debug
// one, where the grid search runs unoptimised and 500 scenes took 41 s. Shrink the solver's grid in
// orbit_scene.hpp, not this count, if it grows too slow again.
#ifdef NDEBUG
constexpr std::uint64_t sweep_seeds = 100;
#else
constexpr std::uint64_t sweep_seeds = 10;
#endif

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

// How many of four small slips of the hand (half a degree either way, 2 % of the speed either way)
// still win.
int survived_slips(const Level& level, Vec v) {
    const auto turned = [&](double deg) {
        const double a = deg * 3.141592653589793 / 180;
        return Vec{v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)};
    };
    const auto scaled = [&](double k) { return v * std::min(k, max_launch_speed / length(v)); };
    return wins(level, turned(0.5)) + wins(level, turned(-0.5)) + wins(level, scaled(1.02)) +
           wins(level, scaled(0.98));
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

// How many bodies a difficulty's shape asks for in all.
std::size_t body_count(const scene_detail::Shape& shape) {
    return std::size_t(shape.anchors + shape.moons + shape.asteroids);
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
            const scene_detail::Scene scene = scene_detail::generated_level(seed, difficulty);
            check_scene(scene.level, seed, difficulty);
            CHECK_MESSAGE(scene.launch.has_value(),
                          "seed " << seed << ", difficulty " << difficulty);
            if (!scene.launch) continue;
            CHECK_MESSAGE(wins(scene.level, *scene.launch), "seed " << seed);
            CHECK_MESSAGE(survived_slips(scene.level, *scene.launch) >= 2, "seed " << seed << ", difficulty " << difficulty);
        }
}

TEST_CASE("the bare fallback scene can be won") {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        std::mt19937_64 rng(seed);
        const Level fallback = scene_detail::bare_scene(rng);
        CHECK(fallback.bodies.empty());
        CHECK(in_field(fallback.start));
        CHECK(in_field(fallback.target));
        const auto velocity = solve(fallback);
        REQUIRE(velocity.has_value());
        CHECK(wins(fallback, *velocity));
    }
}

TEST_CASE("a harder difficulty asks for more bodies") {
    const scene_detail::Shape hardest = scene_detail::shape_of(5);
    for (std::uint64_t seed : {1, 2, 3, 42}) {
        const Level easy = random_level(seed, 1);
        const Level hard = random_level(seed, 5);
        CHECK_MESSAGE(hard.bodies.size() > easy.bodies.size(), "seed " << seed);
        CHECK(hard.bodies.size() <= body_count(hardest));
        // Nothing was dropped: the hardest scene must have the anchors its shape asks for.
        if (hard.bodies.size() == body_count(hardest)) {
            const auto anchors = std::count_if(hard.bodies.begin(), hard.bodies.end(),
                                               [](const Body& body) { return body.anchor; });
            CHECK(std::size_t(anchors) == std::size_t(hardest.anchors));
        }
    }
}
