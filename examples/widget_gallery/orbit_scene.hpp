// Orbit's pure scene work: a solver that finds a launch velocity a level can be won with, and a
// seeded generator that only hands out levels the solver can win. No widget and no clock in here,
// just plain functions over plain values (tests/test_orbit_scene.cpp); the tab calls them.
#pragma once

#include "orbit_physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace orbit {

namespace scene_detail {

inline constexpr double two_pi = 6.283185307179586;
inline constexpr int grid_directions = 180;   // the solver's directions around the circle
inline constexpr int grid_speeds = 10;        // its speeds, from 20 % to 100 % of the top speed
inline constexpr double slowest_share = 0.2;
inline constexpr int generation_tries = 50;   // scenes drawn before the fallback is used

inline constexpr double edge_margin = 60;   // how far bodies stay from the field's edges
inline constexpr double clearance = 60;     // how far bodies stay from the start and the ring
inline constexpr double body_gap = 20;      // how far bodies stay apart at launch
inline constexpr int placement_tries = 40;  // draws spent on one body before it is dropped

// A flight's outcome for `velocity` at `gravity`: stepped until it stops flying. The lost rules
// bound the steps anyway; this cap only keeps the loop finite should they ever not.
inline Outcome flight_outcome(const Level& level, Vec velocity, double gravity) {
    World w = launch(level, velocity);
    constexpr int max_steps = static_cast<int>(flight_limit / fixed_step) + 2;
    for (int i = 0; i < max_steps; ++i)
        if (const Outcome outcome = step(w, level, gravity, fixed_step); outcome != Outcome::Flying)
            return outcome;
    return Outcome::Flying;
}

// The grid's k-th speed: the slowest share of the top speed, up to the whole of it.
inline double grid_speed(int k) {
    return max_launch_speed * (slowest_share + (1.0 - slowest_share) * k / (grid_speeds - 1));
}

inline bool inside_field(Vec p) {
    return p.x > edge_margin && p.x < field_width - edge_margin && p.y > edge_margin &&
           p.y < field_height - edge_margin;
}

// One uniform double in [0, 1) straight from the generator's bits: the standard distributions map
// them differently on different compilers, and one seed must give one scene everywhere.
inline double unit(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

inline double between(std::mt19937_64& rng, double low, double high) {
    return low + (high - low) * unit(rng);
}

// Whether a body of `radius` at `pos` leaves room for the start, the ring and every body placed so
// far.
inline bool room_for(const Level& level, Vec pos, double radius) {
    if (!inside_field(pos)) return false;
    if (length(pos - level.start) <= radius + clearance) return false;
    if (length(pos - level.target) <= radius + level.target_radius + clearance) return false;
    for (const auto& other : level.bodies)
        if (length(pos - other.pos) <= radius + other.radius + body_gap) return false;
    return true;
}

// The probe waits on the left, the ring sits on the right, both with some jitter.
inline void place_ends(Level& level, std::mt19937_64& rng) {
    level.start = {between(rng, 70, 170), between(rng, 90, field_height - 90)};
    level.target = {between(rng, field_width - 170, field_width - 70),
                    between(rng, 90, field_height - 90)};
    level.target_radius = between(rng, 26, 34);
}

// Draws until one fits; a body that finds no room in `placement_tries` draws is left out, which
// keeps generation bounded.
template <class Place>
inline bool add_body(Level& level, Place place) {
    for (int attempt = 0; attempt < placement_tries; ++attempt)
        if (auto body = place(level)) {
            level.bodies.push_back(*body);
            return true;
        }
    return false;
}

inline std::optional<Body> anchor_at(const Level& level, std::mt19937_64& rng) {
    const Vec pos{between(rng, 200, field_width - 200), between(rng, 90, field_height - 90)};
    const double mass = between(rng, 0.6, 1.6);
    const double radius = between(rng, 26, 46);
    if (!room_for(level, pos, radius)) return std::nullopt;
    return Body{.pos = pos, .mass = mass, .radius = radius};
}

// A moon on a circular orbit around the anchor at `anchor`.
inline std::optional<Body> moon_around(const Level& level, std::mt19937_64& rng,
                                       std::size_t anchor) {
    const double distance = level.bodies[anchor].radius + between(rng, 45, 80);
    const double phase = unit(rng);
    const double mass = between(rng, 0.15, 0.5);
    const double radius = between(rng, 9, 16);
    const bool clockwise = unit(rng) < 0.5;
    const Body moon =
        orbiting(level.bodies, anchor, distance, phase, mass, radius, clockwise);
    return room_for(level, moon.pos, moon.radius) ? std::optional<Body>{moon} : std::nullopt;
}

// A rock drifting on its own, under everyone's gravity, rather than orbiting one anchor.
inline std::optional<Body> drifting_rock(const Level& level, std::mt19937_64& rng) {
    const Vec pos{between(rng, 200, field_width - 200), between(rng, 90, field_height - 90)};
    const double radius = between(rng, 9, 18);
    if (!room_for(level, pos, radius)) return std::nullopt;
    const double heading = between(rng, 0, two_pi);
    const double speed = between(rng, 20, 70);
    const double mass = between(rng, 0.2, 0.6);
    return Body{.pos = pos,
                .vel = {std::cos(heading) * speed, std::sin(heading) * speed},
                .mass = mass,
                .radius = radius,
                .anchor = false};
}

// How many of each kind of body a difficulty asks for.
struct Shape {
    int anchors = 1, moons = 0, asteroids = 0;
};

inline Shape shape_of(int difficulty) {
    switch (difficulty) {
    case 2: return {1, 1, 0};
    case 3: return {2, 1, 1};
    case 4: return {2, 2, 1};
    case 5: return {3, 2, 2};
    default: return {1, 0, 0};
    }
}

inline Level make_scene(std::mt19937_64& rng, Shape shape) {
    Level level{};
    place_ends(level, rng);
    // simplify: a random scene's ring stays at a fixed point; a carried one has to be wider than
    // its moon to be reachable, which is worth teaching in a hand-made level first.
    std::vector<std::size_t> anchors;
    for (int i = 0; i < shape.anchors; ++i)
        if (add_body(level, [&](const Level& l) { return anchor_at(l, rng); }))
            anchors.push_back(level.bodies.size() - 1);
    for (int i = 0; i < shape.moons && !anchors.empty(); ++i) {
        const std::size_t anchor = anchors[rng() % anchors.size()];
        add_body(level, [&](const Level& l) { return moon_around(l, rng, anchor); });
    }
    for (int i = 0; i < shape.asteroids; ++i)
        add_body(level, [&](const Level& l) { return drifting_rock(l, rng); });
    return level;
}

} // namespace scene_detail

// A winning launch velocity for `level` at `gravity`, if one is found among a fixed grid of
// directions × speeds (each simulated to its outcome); nothing when the whole grid fails. The grid
// starts on the ring, so the shot the level looks like it wants is tried first, then 2° steps
// around the circle; every direction tries the slowest speed first.
inline std::optional<Vec> solve(const Level& level, double gravity = 1.0) {
    const Vec aim = ring_centre(launch(level, {}), level);
    const double first = std::atan2(aim.y - level.start.y, aim.x - level.start.x);
    for (int direction = 0; direction < scene_detail::grid_directions; ++direction) {
        const double angle =
            first + scene_detail::two_pi * direction / scene_detail::grid_directions;
        const Vec heading{std::cos(angle), std::sin(angle)};
        for (int speed = 0; speed < scene_detail::grid_speeds; ++speed) {
            const Vec velocity = heading * scene_detail::grid_speed(speed);
            if (scene_detail::flight_outcome(level, velocity, gravity) == Outcome::Hit)
                return velocity;
        }
    }
    return std::nullopt;
}

// The same scene for the same seed; difficulty 1-5 sets how many anchors, moons and asteroids.
// Only scenes `solve` can win are handed out; after 50 unsolvable tries, the simplest winnable
// fallback.
inline Level random_level(std::uint64_t seed, int difficulty) {
    std::mt19937_64 rng(seed);
    const scene_detail::Shape shape = scene_detail::shape_of(std::clamp(difficulty, 1, 5));
    for (int attempt = 0; attempt < scene_detail::generation_tries; ++attempt) {
        Level level = scene_detail::make_scene(rng, shape);
        if (solve(level)) return level;
    }
    // Two points and nothing between them: a shot aimed at the ring crosses it at the grid's
    // first speed whatever the ends are, so this fallback is always winnable.
    Level fallback{};
    scene_detail::place_ends(fallback, rng);
    return fallback;
}

} // namespace orbit
