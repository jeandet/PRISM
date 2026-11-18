// Orbit's hand-made levels, one scene each: where the probe waits, where the ring is, and the
// bodies in the way. Plain values (orbit_physics.hpp has what they mean); the tab plays them.
#pragma once

#include "orbit_physics.hpp"

#include <vector>

namespace orbit {

inline const std::vector<Level>& levels() {
    static const std::vector<Level> all{
        {.start = {90, 300}, .target = {900, 300}, .target_radius = 34,
         .bodies = {{.pos = {500, 300}, .mass = 1.0, .radius = 46}}},
        {.start = {90, 480}, .target = {880, 130}, .target_radius = 30,
         .bodies = {{.pos = {420, 260}, .mass = 1.2, .radius = 40}, {.pos = {660, 420}, .mass = 0.8, .radius = 32}}},
        {.start = {90, 120}, .target = {900, 470}, .target_radius = 28,
         .bodies = {{.pos = {330, 330}, .mass = 0.9, .radius = 34}, {.pos = {560, 160}, .mass = 1.4, .radius = 44},
                    {.pos = {740, 380}, .mass = 0.7, .radius = 28}}},
        {.start = {90, 300}, .target = {920, 300}, .target_radius = 26,
         .bodies = {{.pos = {380, 300}, .mass = 1.6, .radius = 50}, {.pos = {640, 170}, .mass = 0.9, .radius = 30},
                    {.pos = {640, 430}, .mass = 0.9, .radius = 30}, {.pos = {830, 300}, .mass = 0.5, .radius = 22}}},
    };
    return all;
}

} // namespace orbit
