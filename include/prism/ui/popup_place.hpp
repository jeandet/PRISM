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
