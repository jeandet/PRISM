// Orbit's pure scene work: a solver that finds a launch velocity a level can be won with, and a
// seeded generator that only hands out levels the solver can win. No widget and no clock in here,
// just plain functions over plain values (tests/test_orbit_scene.cpp); the tab calls them.
#pragma once

#include "orbit_physics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <random>
#include <vector>

namespace orbit {

namespace scene_detail {

inline constexpr double two_pi = 6.283185307179586;
inline constexpr int grid_directions = 120;   // the solver's directions around the circle
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
    World w = launch(level, velocity, gravity);
    constexpr int max_steps = static_cast<int>(flight_limit / fixed_step) + 2;
    for (int i = 0; i < max_steps; ++i)
        if (const Outcome outcome = step(w, level, gravity, fixed_step); outcome != Outcome::Flying)
            return outcome;
    return Outcome::Flying;
}

// The slips of a player's hand a winning shot must survive: a turn of the direction and a share of
// the speed.
struct Slip {
    double turn_degrees, speed_share;
};
inline constexpr Slip slips[] = {{0.5, 1.0}, {-0.5, 1.0}, {0.0, 1.02}, {0.0, 0.98}};
inline constexpr int slips_to_survive = 2;

inline Vec slipped(Vec velocity, Slip slip) {
    const double a = slip.turn_degrees * two_pi / 360;
    const Vec turned{velocity.x * std::cos(a) - velocity.y * std::sin(a),
                     velocity.x * std::sin(a) + velocity.y * std::cos(a)};
    const double speed = std::min(length(turned) * slip.speed_share, max_launch_speed);
    return turned * (speed / length(turned));
}

// Whether a shot is one a player could repeat: it still hits after at least `slips_to_survive` of
// the slips.
inline bool robust(const Level& level, Vec velocity, double gravity) {
    int hits = 0, misses = 0;
    for (const Slip slip : slips) {
        const bool hit = flight_outcome(level, slipped(velocity, slip), gravity) == Outcome::Hit;
        (hit ? hits : misses) += 1;
        if (hits >= slips_to_survive) return true;
        if (misses > static_cast<int>(std::size(slips)) - slips_to_survive) return false;
    }
    return false;
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

// Draws until one fits; a body that finds no room in `placement_tries` draws is left out rather
// than retried forever, which keeps generation bounded.
// simplify: a dropped body makes a hard scene easier than its shape promises; give placement a
// retry budget of its own when scenes start to look sparse.
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

// Two ends and nothing between them: the fallback when 50 scenes in a row cannot be won. A shot
// aimed at the ring crosses it at the grid's first speed whatever the ends are.
inline Level bare_scene(std::mt19937_64& rng) {
    Level level{};
    place_ends(level, rng);
    return level;
}

} // namespace scene_detail

// A winning launch velocity for `level` at `gravity`, if one is found among a fixed grid of
// directions × speeds (each simulated to its outcome, and a hit counts only if the shot is robust to
// a small slip of the hand); nothing when the whole grid fails. The grid
// starts on the ring, so the shot the level looks like it wants is tried first, then 3° steps
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
            if (scene_detail::flight_outcome(level, velocity, gravity) == Outcome::Hit &&
                scene_detail::robust(level, velocity, gravity))
                return velocity;
        }
    }
    return std::nullopt;
}

namespace scene_detail {

// What the generator hands back: the scene and the launch it was proved winnable with.
struct Scene {
    Level level{};
    std::optional<Vec> launch{};
};

// simplify: "one seed, one scene everywhere" holds within one maths library: cos, sin, hypot and exp
// differ in their last bits between glibc, musl/wasm and MSVC, so a borderline scene can differ across
// platforms. Fine for a game; compare scenes only on one platform.
// The scene for a seed, with the velocity the generator already won it with, so a replay costs one
// flight rather than another search.
inline Scene generated_level(std::uint64_t seed, int difficulty) {
    std::mt19937_64 rng(seed);
    const Shape shape = shape_of(std::clamp(difficulty, 1, 5));
    for (int attempt = 0; attempt < generation_tries; ++attempt) {
        Level level = make_scene(rng, shape);
        if (auto launch = solve(level)) return {.level = level, .launch = launch};
    }
    Level fallback = bare_scene(rng);
    return {.level = fallback, .launch = solve(fallback)};
}

} // namespace scene_detail

// The same scene for the same seed; difficulty 1-5 sets how many anchors, moons and asteroids.
// Only scenes `solve` can win are handed out; after 50 unsolvable tries, the simplest winnable
// fallback.
inline Level random_level(std::uint64_t seed, int difficulty) {
    return scene_detail::generated_level(seed, difficulty).level;
}

} // namespace orbit
