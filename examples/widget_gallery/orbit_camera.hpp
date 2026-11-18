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

// The field and the probe, a margin around both.
inline Box shown_box(const std::optional<Vec>& probe) {
    Vec low{0, 0}, high{field_width, field_height};
    if (probe) {
        low = {std::min(low.x, probe->x), std::min(low.y, probe->y)};
        high = {std::max(high.x, probe->x), std::max(high.y, probe->y)};
    }
    return {.low = low - Vec{camera_margin, camera_margin},
            .high = high + Vec{camera_margin, camera_margin}};
}

// The narrowest view of `aspect` that holds the box, never past the lost box: at a portrait aspect
// the height is the tighter of the two, so the width budget follows the aspect.
inline double view_width(const Box& box, double aspect) {
    const double need = std::max(box.high.x - box.low.x, (box.high.y - box.low.y) * aspect);
    const double room = 2 * lost_reach * field_diagonal();
    return std::min(need, std::min(room, room * aspect));
}

// `value` moved into `[low, high]`. A narrow aspect makes the view taller than it is wide, so its
// height can pass the lost box; the range is clamped rather than trusted to be ordered.
inline double pushed_into(double value, double low, double high) {
    return std::min(std::max(value, low), high);
}

// The box's centre, moved just enough for the whole view to stay in the lost box.
inline Vec view_centre(const Box& box, double width, double aspect) {
    const Vec middle = (box.low + box.high) * 0.5;
    const Vec lost = field_centre();
    const double reach = lost_reach * field_diagonal();
    const double half = width / 2, half_height = width / aspect / 2;
    return {pushed_into(middle.x, lost.x - reach + half, lost.x + reach - half),
            pushed_into(middle.y, lost.y - reach + half_height, lost.y + reach - half_height)};
}

} // namespace camera_detail

// Where the camera wants to be: the field and the probe, plus a margin, in the smallest rectangle of
// `aspect` that holds them, never past the lost box. A canvas not yet laid out counts as square.
inline View wanted_view(const std::optional<Vec>& probe, double aspect) {
    const double shape = aspect > 0 ? aspect : 1;
    const camera_detail::Box box = camera_detail::shown_box(probe);
    const double width = camera_detail::view_width(box, shape);
    return {.centre = camera_detail::view_centre(box, width, shape), .width = width};
}

// One frame of `seconds` of easing from `now` toward `wanted`, centre and width alike: each closes
// the same share of its gap, so the time constant alone says how quickly the camera catches up.
inline View eased(View now, View wanted, double seconds) {
    const double closed = 1 - std::exp(-seconds / camera_time_constant);
    return {.centre = now.centre + (wanted.centre - now.centre) * closed,
            .width = now.width + (wanted.width - now.width) * closed};
}

} // namespace orbit
