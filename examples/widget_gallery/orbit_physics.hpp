// Orbit's physics: a world of bodies and a massless probe, plain values and functions with no
// widget in them (tests/test_orbit.cpp). Anchors never move; the probe and the moving bodies
// fall under everyone's gravity, stepped with velocity Verlet so a launch replays exactly.
// The hand-made levels are orbit_levels.hpp; the tab that draws it all is orbit_tab.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace orbit {

// The field, in its own units; the canvas scales it to whatever size it is given.
inline constexpr double field_width = 1000, field_height = 600;
inline constexpr double max_launch_speed = 520;   // field units per second
inline constexpr double launch_gain = 3.0;        // speed per unit of drag
inline constexpr double gravity_constant = 4e6;   // a mass-1 body pulls 400 units/s² at 100 units
inline constexpr double fixed_step = 1.0 / 240.0; // the step every flight is made of, in seconds
inline constexpr double flight_limit = 30;        // a flight longer than this is lost, in seconds
inline constexpr double lost_reach = 1.5;         // times the field's diagonal from its centre

struct Vec {
    double x = 0, y = 0;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
inline Vec operator*(Vec a, double k) { return {a.x * k, a.y * k}; }
inline double length(Vec v) { return std::hypot(v.x, v.y); }

inline Vec field_centre() { return {field_width / 2, field_height / 2}; }
inline double field_diagonal() { return std::hypot(field_width, field_height); }

struct Body {
    Vec pos{}, vel{};   // `vel` is the launch state; an anchor ignores it
    double mass = 1, radius = 30;
    bool anchor = true;   // an anchor never moves, whatever hits it
};

struct Probe {
    Vec pos{}, vel{};
};

// `Level` sits with the world it describes because step and advance take it; the hand-made levels
// themselves are orbit_levels.hpp.
struct Level {
    Vec start{};                              // where the probe waits
    Vec target{};                             // the ring's centre, unless a body carries it
    double target_radius = 30;
    std::optional<std::size_t> target_body{}; // the ring rides this body
    std::vector<Body> bodies{};
};

struct World {
    std::vector<Body> bodies{};   // as the level left them, then moved by the flight
    Probe probe{};
    double time = 0;              // seconds flown since the launch
};

enum class Outcome { Flying, Hit, Crashed, Lost };

// Every body's pull on a point at `at`, scaled by the `gravity` slider. A body feels nothing from
// itself: its own distance is zero, so its share is zero.
inline Vec acceleration(const std::vector<Body>& bodies, Vec at, double gravity) {
    Vec sum{};
    for (const auto& b : bodies) {
        Vec d = b.pos - at;
        double r = std::max(length(d), b.radius);   // never infinite, even at the center
        sum = sum + d * (gravity * gravity_constant * b.mass / (r * r * r));
    }
    return sum;
}

// One velocity-Verlet step (kick, drift, kick): the acceleration is re-read where the drift lands.
template <class Accel>
inline void verlet(Vec& pos, Vec& vel, double dt, const Accel& acceleration_at) {
    vel = vel + acceleration_at(pos) * (dt / 2);
    pos = pos + vel * dt;
    vel = vel + acceleration_at(pos) * (dt / 2);
}

// A fresh flight: the level's bodies as they start, and the probe at the start with `velocity`.
inline World launch(const Level& level, Vec velocity) {
    return {.bodies = level.bodies,
            .probe = {.pos = level.start, .vel = velocity},
            .time = 0};
}

// A body in a circular orbit around `bodies[around]` at `distance`, starting `phase` turns in,
// counter-clockwise on the canvas (which has y pointing down) unless `clockwise`; the speed is the
// circular one for gravity 1.
inline Body orbiting(const std::vector<Body>& bodies, std::size_t around, double distance, double phase,
                     double mass, double radius, bool clockwise = false) {
    constexpr double two_pi = 6.283185307179586;
    const double angle = two_pi * phase;
    const Vec out{std::cos(angle), std::sin(angle)};
    const Vec tangent{out.y, -out.x};   // a quarter turn from `out`, the way an orbit goes
    const double speed = std::sqrt(gravity_constant * bodies[around].mass / distance);
    return {.pos = bodies[around].pos + out * distance,
            .vel = tangent * (clockwise ? -speed : speed),
            .mass = mass,
            .radius = radius,
            .anchor = false};
}

// Where the ring is now: at its fixed point, or on the body carrying it.
inline Vec ring_centre(const World& w, const Level& level) {
    if (!level.target_body) return level.target;
    // simplify: the carrier is an index into `bodies`, which collisions renumber, so a scene keeps
    // its carrier clear of anything that could destroy it; give the ring a body id instead if a
    // carrier must be able to die.
    const std::size_t i = *level.target_body;
    return i < w.bodies.size() ? w.bodies[i].pos : level.target;
}

// What became of the probe where it is now.
inline Outcome probe_outcome(const World& w, const Level& level) {
    for (const auto& b : w.bodies)
        if (length(w.probe.pos - b.pos) < b.radius) return Outcome::Crashed;
    if (length(w.probe.pos - ring_centre(w, level)) < level.target_radius) return Outcome::Hit;
    if (w.time > flight_limit) return Outcome::Lost;
    if (length(w.probe.pos - field_centre()) > lost_reach * field_diagonal()) return Outcome::Lost;
    return Outcome::Flying;
}

// Which bodies touch another body this step: a moving body that does is gone, an anchor survives.
inline std::vector<bool> collided(const std::vector<Body>& bodies) {
    std::vector<bool> gone(bodies.size(), false);
    for (std::size_t i = 0; i < bodies.size(); ++i)
        for (std::size_t j = i + 1; j < bodies.size(); ++j)
            if (length(bodies[i].pos - bodies[j].pos) < bodies[i].radius + bodies[j].radius) {
                gone[i] = gone[i] || !bodies[i].anchor;
                gone[j] = gone[j] || !bodies[j].anchor;
            }
    return gone;
}

// The bodies left after this step's collisions, in order.
inline void drop_collided_bodies(std::vector<Body>& bodies) {
    const std::vector<bool> gone = collided(bodies);
    std::size_t kept = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i)
        if (!gone[i]) bodies[kept++] = bodies[i];
    bodies.resize(kept);
}

// One step of `dt` seconds for the moving bodies and the probe, then what became of the probe.
inline Outcome step(World& w, const Level& level, double gravity, double dt) {
    const auto pull = [&](Vec at) { return acceleration(w.bodies, at, gravity); };
    for (auto& b : w.bodies)
        if (!b.anchor) verlet(b.pos, b.vel, dt, pull);
    verlet(w.probe.pos, w.probe.vel, dt, pull);
    w.time += dt;
    const Outcome outcome = probe_outcome(w, level);
    drop_collided_bodies(w.bodies);
    return outcome;
}

// `seconds` of real time in fixed steps of `dt`, however often the clock ticks: `carry` keeps
// what is left shorter than a step for the next call (a fixed-timestep loop).
inline Outcome advance(World& w, const Level& level, double gravity, double seconds, double& carry,
                       double dt = fixed_step) {
    carry += seconds;
    while (carry >= dt) {
        carry -= dt;
        if (auto outcome = step(w, level, gravity, dt); outcome != Outcome::Flying) return outcome;
    }
    return Outcome::Flying;
}

// A slingshot: pulling back from `start` to `release` launches the other way, faster the further
// it was pulled, up to the top speed.
inline Vec launch_velocity(Vec start, Vec release) {
    Vec v = (start - release) * launch_gain;
    double speed = length(v);
    return speed > max_launch_speed ? v * (max_launch_speed / speed) : v;
}

} // namespace orbit
