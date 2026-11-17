// Orbit, the gallery's game: launch a probe past planets into a target ring. This file is its
// physics, plain values and functions with no widget in them (tests/test_orbit.cpp); the tab
// that draws and plays it is orbit_tab.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace orbit {

// The field, in its own units; the canvas scales it to whatever size it is given.
inline constexpr double field_width = 1000, field_height = 600;
inline constexpr double max_launch_speed = 520;   // field units per second
inline constexpr double launch_gain = 3.0;        // speed per unit of drag
inline constexpr double gravity_constant = 4e6;   // a mass-1 planet pulls 400 units/s² at 100 units

struct Vec {
    double x = 0, y = 0;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
inline Vec operator*(Vec a, double k) { return {a.x * k, a.y * k}; }
inline double length(Vec v) { return std::hypot(v.x, v.y); }

struct Planet {
    Vec pos;
    double mass = 1;
    double radius = 30;
};

struct Level {
    Vec start;               // where the probe waits
    Vec target;              // the ring's center
    double target_radius = 30;
    std::vector<Planet> planets;
};

struct Probe {
    Vec pos;
    Vec vel;
};

enum class Outcome { Flying, Hit, Crashed, Lost };

// Every planet's pull on a probe at `at`, scaled by the `gravity` slider.
inline Vec acceleration(Vec at, const Level& level, double gravity) {
    Vec a;
    for (const auto& p : level.planets) {
        Vec d = p.pos - at;
        double r = std::max(length(d), p.radius);   // never infinite, even at the center
        a = a + d * (gravity * gravity_constant * p.mass / (r * r * r));
    }
    return a;
}

// One step of `dt` seconds (semi-implicit Euler: the velocity first, then the position, which
// keeps an orbit from spiralling out), then what became of the probe.
inline Outcome step(Probe& probe, const Level& level, double gravity, double dt) {
    probe.vel = probe.vel + acceleration(probe.pos, level, gravity) * dt;
    probe.pos = probe.pos + probe.vel * dt;
    for (const auto& p : level.planets)
        if (length(probe.pos - p.pos) < p.radius) return Outcome::Crashed;
    if (length(probe.pos - level.target) < level.target_radius) return Outcome::Hit;
    constexpr double margin = 200;   // a probe a little off screen may still swing back
    bool outside = probe.pos.x < -margin || probe.pos.x > field_width + margin ||
                   probe.pos.y < -margin || probe.pos.y > field_height + margin;
    return outside ? Outcome::Lost : Outcome::Flying;
}

// `seconds` of real time in fixed steps of `dt`, however often the clock ticks: `carry` keeps
// what is left shorter than a step for the next call (a fixed-timestep loop).
inline Outcome advance(Probe& probe, const Level& level, double gravity, double seconds, double& carry,
                       double dt = 1.0 / 240.0) {
    carry += seconds;
    while (carry >= dt) {
        carry -= dt;
        if (auto o = step(probe, level, gravity, dt); o != Outcome::Flying) return o;
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

inline const std::vector<Level>& levels() {
    static const std::vector<Level> all{
        {.start = {90, 300}, .target = {900, 300}, .target_radius = 34,
         .planets = {{.pos = {500, 300}, .mass = 1.0, .radius = 46}}},
        {.start = {90, 480}, .target = {880, 130}, .target_radius = 30,
         .planets = {{.pos = {420, 260}, .mass = 1.2, .radius = 40}, {.pos = {660, 420}, .mass = 0.8, .radius = 32}}},
        {.start = {90, 120}, .target = {900, 470}, .target_radius = 28,
         .planets = {{.pos = {330, 330}, .mass = 0.9, .radius = 34}, {.pos = {560, 160}, .mass = 1.4, .radius = 44},
                     {.pos = {740, 380}, .mass = 0.7, .radius = 28}}},
        {.start = {90, 300}, .target = {920, 300}, .target_radius = 26,
         .planets = {{.pos = {380, 300}, .mass = 1.6, .radius = 50}, {.pos = {640, 170}, .mass = 0.9, .radius = 30},
                     {.pos = {640, 430}, .mass = 0.9, .radius = 30}, {.pos = {830, 300}, .mass = 0.5, .radius = 22}}},
    };
    return all;
}

} // namespace orbit
