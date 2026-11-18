// Orbit's hand-made levels, one idea each, in the order a player meets them: where the probe waits,
// where the ring is, and the bodies in the way. Plain values (orbit_physics.hpp has what they
// mean); the tab plays them and tests/test_orbit_scene.cpp checks every one can be won.
#pragma once

#include "orbit_physics.hpp"

#include <vector>

namespace orbit {

namespace level_detail {

inline Body anchor(Vec pos, double mass, double radius) {
    return {.pos = pos, .mass = mass, .radius = radius};
}

inline Level slingshot() {
    return {.start = {90, 300}, .target = {900, 300}, .target_radius = 34,
            .bodies = {anchor({500, 300}, 1.0, 46)}};
}

inline Level two_planets() {
    return {.start = {90, 480}, .target = {880, 130}, .target_radius = 30,
            .bodies = {anchor({420, 260}, 1.2, 40), anchor({660, 420}, 0.8, 32)}};
}

inline Level between_two() {
    return {.start = {90, 300}, .target = {900, 300}, .target_radius = 30,
            .bodies = {anchor({480, 190}, 1.2, 44), anchor({480, 410}, 1.2, 44)}};
}

inline Level first_moon() {
    Level lv{.start = {90, 500}, .target = {900, 120}, .target_radius = 30,
             .bodies = {anchor({500, 300}, 1.4, 44)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 140, 0.0, 0.05, 12));
    return lv;
}

inline Level moons_pull() {
    Level lv{.start = {90, 90}, .target = {900, 520}, .target_radius = 28,
             .bodies = {anchor({480, 300}, 1.0, 40)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 170, 0.5, 0.6, 20));
    return lv;
}

inline Level ring_on_a_moon() {
    Level lv{.start = {90, 300}, .target = {}, .target_radius = 34, .target_body = 1,
             .bodies = {anchor({600, 300}, 1.4, 44)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 170, 0.25, 0.05, 14));
    return lv;
}

inline Level asteroid_belt() {
    Level lv{.start = {90, 300}, .target = {910, 300}, .target_radius = 30, .bodies = {}};
    for (int i = 0; i < 7; ++i) lv.bodies.push_back(anchor({500, 60.0 + 80 * i}, 0.08, 14));
    return lv;
}

inline Level two_moons() {
    Level lv{.start = {90, 520}, .target = {910, 110}, .target_radius = 30,
             .bodies = {anchor({500, 300}, 1.5, 46)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 120, 0.0, 0.05, 12));
    lv.bodies.push_back(orbiting(lv.bodies, 0, 200, 0.5, 0.1, 16, true));
    return lv;
}

inline Level binary() {
    Level lv{.start = {90, 520}, .target = {910, 100}, .target_radius = 28,
             .bodies = {anchor({440, 300}, 1.0, 38), anchor({620, 300}, 1.0, 38)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 100, 0.75, 0.05, 12));
    return lv;
}

inline Level everything() {
    Level lv{.start = {90, 300}, .target = {}, .target_radius = 28, .target_body = 3,
             .bodies = {anchor({360, 200}, 1.2, 40), anchor({640, 400}, 1.2, 40)}};
    lv.bodies.push_back(orbiting(lv.bodies, 0, 110, 0.1, 0.05, 12));
    lv.bodies.push_back(orbiting(lv.bodies, 1, 130, 0.6, 0.08, 14, true));
    lv.bodies.push_back(anchor({500, 300}, 0.08, 14));
    lv.bodies.push_back(anchor({800, 150}, 0.08, 14));
    return lv;
}

} // namespace level_detail

inline const std::vector<Level>& levels() {
    namespace d = level_detail;
    static const std::vector<Level> all{d::slingshot(),  d::two_planets(),   d::between_two(),
                                        d::first_moon(), d::moons_pull(),    d::ring_on_a_moon(),
                                        d::asteroid_belt(), d::two_moons(),  d::binary(),
                                        d::everything()};
    return all;
}

} // namespace orbit
