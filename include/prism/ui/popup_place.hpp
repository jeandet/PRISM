#pragma once

#include <prism/core/types.hpp>

#include <algorithm>

// Where a popup goes next to what it opened from, kept inside the window. Used by menus
// (app/event_routing.hpp) and the dropdown.
namespace prism::ui {

// One axis of place_popup: after the anchor if it fits there, else before it if it fits there,
// else on the roomier side; then kept inside [0, window].
inline float place_on_axis(float size, float anchor_start, float anchor_end, float window) {
    float after = anchor_end, before = anchor_start - size;
    float pos = after + size <= window ? after
              : before >= 0.f        ? before
              : window - anchor_end >= anchor_start ? after : before;
    return std::clamp(pos, 0.f, std::max(0.f, window - size));
}

// Where a `size` popup opening at a widget goes, in the widget's own coordinates: below it (or
// above, see place_on_axis), starting at its left edge but slid left to stay inside the window.
// `node` is a WidgetNode: its position in the window, its zoom there and the window's size, as of
// the last layout.
template <typename Node>
core::Point popup_origin(const Node& node, core::Size size, float anchor_h) {
    if (node.viewport_height.raw() <= 0) return core::Point{core::X{0}, core::Y{anchor_h}};
    const float s = node.view_scale;
    float left = node.absolute_x.raw(), top = node.absolute_y.raw();
    float x = std::clamp(left, 0.f, std::max(0.f, node.viewport_width.raw() - size.w.raw() * s));
    float y = place_on_axis(size.h.raw() * s, top, top + anchor_h * s, node.viewport_height.raw());
    return core::Point{core::X{(x - left) / s}, core::Y{(y - top) / s}};
}

// A popup of `size` next to `anchor` (window coordinates): below it, or above when only that
// fits; left-aligned with it, or right-aligned when only that fits.
// simplify: a popup taller than the window is pinned to the top and its last rows stay
// unreachable: menus would need scrolling for that.
inline core::Rect place_popup(core::Size size, core::Rect anchor, core::Size window) {
    float ax = anchor.origin.x.raw(), aw = anchor.extent.w.raw();
    // Horizontally the popup starts at the anchor's left (not after its right edge), so the
    // "anchor" on this axis is the anchor's right edge read backwards: [ax + aw - w, ax].
    float x = ax + size.w.raw() <= window.w.raw() ? ax
            : ax + aw - size.w.raw() >= 0.f       ? ax + aw - size.w.raw()
            : ax;
    x = std::clamp(x, 0.f, std::max(0.f, window.w.raw() - size.w.raw()));
    float y = place_on_axis(size.h.raw(), anchor.origin.y.raw(),
                            anchor.origin.y.raw() + anchor.extent.h.raw(), window.h.raw());
    return core::Rect{core::Point{core::X{x}, core::Y{y}}, size};
}

} // namespace prism::ui
