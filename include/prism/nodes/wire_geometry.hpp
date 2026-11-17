#pragma once

#include <prism/core/types.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace prism::nodes {

struct WireCurve { core::Point p0, c0, c1, p1; };

namespace wire_detail {

inline constexpr float min_reach = 40.f;

inline core::Point at(float x, float y) { return {core::X{x}, core::Y{y}}; }

inline float distance_to_segment(core::Point p, core::Point a, core::Point b) {
    float dx = b.x.raw() - a.x.raw(), dy = b.y.raw() - a.y.raw();
    float len2 = dx * dx + dy * dy;
    float t = len2 == 0.f ? 0.f
        : std::clamp(((p.x.raw() - a.x.raw()) * dx + (p.y.raw() - a.y.raw()) * dy) / len2, 0.f, 1.f);
    return std::hypot(p.x.raw() - (a.x.raw() + t * dx), p.y.raw() - (a.y.raw() + t * dy));
}

} // namespace wire_detail

// Leaves `from` rightwards and enters `to` from the left, so a backward wire still loops out of its port.
inline WireCurve wire_curve(core::Point from, core::Point to) {
    float reach = std::max(wire_detail::min_reach, std::abs(to.x.raw() - from.x.raw()) / 2.f);
    return {from, wire_detail::at(from.x.raw() + reach, from.y.raw()),
            wire_detail::at(to.x.raw() - reach, to.y.raw()), to};
}

inline core::Point point_on(const WireCurve& w, float t) {
    float u = 1.f - t;
    float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    return wire_detail::at(
        b0 * w.p0.x.raw() + b1 * w.c0.x.raw() + b2 * w.c1.x.raw() + b3 * w.p1.x.raw(),
        b0 * w.p0.y.raw() + b1 * w.c0.y.raw() + b2 * w.c1.y.raw() + b3 * w.p1.y.raw());
}

inline std::vector<core::Point> polyline(const WireCurve& w, int segments = 24) {
    std::vector<core::Point> pts;
    pts.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i)
        pts.push_back(i == segments ? w.p1 : point_on(w, static_cast<float>(i) / static_cast<float>(segments)));
    return pts;
}

// simplify: distance to the 24-segment polyline, not to the true curve; raise `segments` for a tighter hit test.
inline float distance_to(const WireCurve& w, core::Point p) {
    auto pts = polyline(w);
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < pts.size(); ++i)
        best = std::min(best, wire_detail::distance_to_segment(p, pts[i - 1], pts[i]));
    return best;
}

} // namespace prism::nodes
