#pragma once

#include <prism/core/types.hpp>

#include <span>
#include <vector>

namespace prism::nodes {

// A line the board draws while a snap holds, in graph units.
struct Guide {
    core::Point from, to;
    bool operator==(const Guide&) const = default;
};

// A wire end that would level with the dragged card: the port's y on the dragged card relative
// to the card's top, and the y of the other end (graph units).
struct LevelWith {
    core::DY port_from_top;
    core::Y other_end;
    bool operator==(const LevelWith&) const = default;
};

struct Snapped {
    core::Point at;
    std::vector<Guide> guides;
    bool operator==(const Snapped&) const = default;
};

// `at`: where the pointer puts the card's top-left (unsnapped); `size`: its size; `others`: the
// other cards; `wires`: its wired ports; `reach`: the snap distance in graph units.
Snapped snap(core::Point at, core::Size size, std::span<const core::Rect> others,
             std::span<const LevelWith> wires, float reach);

} // namespace prism::nodes
