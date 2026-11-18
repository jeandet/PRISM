#pragma once

#include <prism/core/types.hpp>
#include <prism/render/draw_list.hpp>

namespace prism::render {

// A zoom of a subtree about a fixed point: q -> origin + scale·(q − origin).
struct ViewTransform {
    core::Point origin{};
    float scale = 1.f;

    core::Point apply(core::Point q) const;
    core::Rect apply(core::Rect r) const;      // origin mapped, extent × scale
};

// Every command of `dl` through `t`: points, rect origins and extents, circle radii, line and
// outline thicknesses (0 = filled stays 0), rounded-rect radii, text size and origin, image
// rects, clip rects.
void scale_draw_list(DrawList& dl, const ViewTransform& t);

} // namespace prism::render
