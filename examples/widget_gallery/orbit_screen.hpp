// Orbit's window onto the field: field units to canvas pixels and back, through the camera's view.
#pragma once

#include "orbit_camera.hpp"

#include <prism/prism.hpp>

namespace gallery {

using namespace prism;

struct OrbitScreen {
    float scale, half_w, half_h;
    orbit::Vec centre;

    OrbitScreen(core::Rect bounds, const orbit::View& view)
        : scale(static_cast<float>(bounds.extent.w.raw() / view.width)),
          half_w(bounds.extent.w.raw() / 2), half_h(bounds.extent.h.raw() / 2), centre(view.centre) {}

    // The canvas's shape; one not yet laid out counts as square.
    static double aspect(core::Rect bounds) {
        return bounds.extent.h.raw() > 0 ? bounds.extent.w.raw() / bounds.extent.h.raw() : 1.0;
    }
    core::Point to_canvas(orbit::Vec v) const {
        return {core::X{half_w + scale * static_cast<float>(v.x - centre.x)},
                core::Y{half_h + scale * static_cast<float>(v.y - centre.y)}};
    }
    orbit::Vec to_field(core::Point p) const {
        return {centre.x + (p.x.raw() - half_w) / scale, centre.y + (p.y.raw() - half_h) / scale};
    }
    float length(double l) const { return scale * static_cast<float>(l); }
};

} // namespace gallery
