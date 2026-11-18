// Orbit's camera: where the view wants to be (the field, and the probe while it flies) and one
// frame of easing toward it. Plain values and functions, no widget (tests/test_orbit_camera.cpp);
// the tab draws through it.
#pragma once

#include "orbit_physics.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace orbit {

inline constexpr double camera_margin = 60;           // field units kept clear around what is shown
inline constexpr double camera_time_constant = 0.25;  // seconds to close 63 % of a gap

// The view in field units: `width`, with the height that keeps the canvas's aspect.
struct View {
    Vec centre{};
    double width = 0;
};

namespace camera_detail {

// What the view has to show, before the margin.
struct Box {
    Vec low{}, high{};
};

// The field and the probe, a margin around both, cut to the lost square: what is past it is lost
// anyway, and the view stays on what can still be won back.
inline Box shown_box(const std::optional<Vec>& probe) {
    Vec low{0, 0}, high{field_width, field_height};
    if (probe) {
        low = {std::min(low.x, probe->x), std::min(low.y, probe->y)};
        high = {std::max(high.x, probe->x), std::max(high.y, probe->y)};
    }
    const Vec margin{camera_margin, camera_margin};
    const Vec reach = Vec{lost_reach, lost_reach} * field_diagonal();
    const Vec lost_low = field_centre() - reach, lost_high = field_centre() + reach;
    return {.low = {std::max(low.x - margin.x, lost_low.x), std::max(low.y - margin.y, lost_low.y)},
            .high = {std::min(high.x + margin.x, lost_high.x), std::min(high.y + margin.y, lost_high.y)}};
}

// The narrowest view of `aspect` that holds the box.
inline double view_width(const Box& box, double aspect) {
    return std::max(box.high.x - box.low.x, (box.high.y - box.low.y) * aspect);
}

} // namespace camera_detail

// Where the camera wants to be: the field and the probe, plus a margin, cut to the lost square, in
// the smallest rectangle of `aspect` that holds them, centred on them. A canvas not yet laid out
// counts as square.
inline View wanted_view(const std::optional<Vec>& probe, double aspect) {
    const camera_detail::Box box = camera_detail::shown_box(probe);
    return {.centre = (box.low + box.high) * 0.5,
            .width = camera_detail::view_width(box, aspect > 0 ? aspect : 1)};
}

// One frame of `seconds` of easing from `now` toward `wanted`, centre and width alike: each closes
// the same share of its gap, so the time constant alone says how quickly the camera catches up.
inline View eased(View now, View wanted, double seconds) {
    const double closed = 1 - std::exp(-seconds / camera_time_constant);
    return {.centre = now.centre + (wanted.centre - now.centre) * closed,
            .width = now.width + (wanted.width - now.width) * closed};
}

} // namespace orbit
