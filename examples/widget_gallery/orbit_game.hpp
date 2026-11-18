// Orbit's game rules that need no window: the stars a win earns, the dotted preview of an aim,
// and how persistence fades and trims what stays drawn. Plain values and functions
// (tests/test_orbit_game.cpp); the tab draws and plays with them.
#pragma once

#include "orbit_physics.hpp"

#include <algorithm>
#include <deque>
#include <vector>

namespace orbit {

inline constexpr double preview_seconds = 1.5;
inline constexpr int preview_every = 12;       // steps between two preview dots: 20 per second
inline constexpr double oldest_share = 0.15;   // how much of its colour the oldest trail keeps

// Three stars for a win with one launch, two for up to three, one for more.
inline int stars_for(int launches) { return launches <= 1 ? 3 : launches <= 3 ? 2 : 1; }

// The path `velocity` would take for its first `preview_seconds`, a point every `preview_every`
// steps from the start; it ends early, on the last position, if the flight does.
inline std::vector<Vec> preview_path(const Level& level, Vec velocity, double gravity) {
    World w = launch(level, velocity, gravity);
    std::vector<Vec> path{w.probe.pos};
    const int steps = static_cast<int>(preview_seconds / fixed_step);
    for (int i = 1; i <= steps; ++i) {
        const bool over = step(w, level, gravity, fixed_step) != Outcome::Flying;
        if (over || i % preview_every == 0) path.push_back(w.probe.pos);
        if (over) break;
    }
    return path;
}

// How many bands a trail of `size` points is drawn in when each takes `band` points: one starts at
// every `band`-th point that has a successor.
inline std::size_t trail_band_count(std::size_t size, std::size_t band) { return (size - 2) / band + 1; }

// How much of its colour a trail keeps: `rank` 0 is the newest of `count` and keeps all of it, the
// oldest keeps `oldest_share`, the ones between fall in a straight line.
inline double fade(std::size_t rank, std::size_t count) {
    if (count < 2) return 1.0;
    return 1.0 - (1.0 - oldest_share) * static_cast<double>(rank) / static_cast<double>(count - 1);
}

// How long of its path a moving body's trail keeps, for a persistence slider of 0 to 20.
inline double trail_seconds(double persistence) { return 2.0 * (1.0 + persistence / 5.0); }

struct TrailPoint {
    double time = 0;
    Vec pos{};
};
using BodyTrails = std::vector<std::deque<TrailPoint>>;   // one trail per body, anchors' empty

// Adds where each live moving body is now, and drops what is older than `keep` seconds.
inline void record_trails(BodyTrails& trails, const World& w, double keep) {
    trails.resize(w.bodies.size());
    for (std::size_t i = 0; i < w.bodies.size(); ++i) {
        if (w.bodies[i].anchor) continue;
        auto& trail = trails[i];
        if (w.bodies[i].alive) trail.push_back({.time = w.time, .pos = w.bodies[i].pos});
        while (!trail.empty() && w.time - trail.front().time > keep) trail.pop_front();
    }
}

} // namespace orbit
