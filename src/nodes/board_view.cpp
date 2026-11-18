#include <prism/nodes/board_view.hpp>

#include <algorithm>
#include <cmath>

namespace prism::nodes {

namespace {

core::Rect bounds(std::span<const core::Rect> cards) {
    const auto left = [](const core::Rect& r) { return r.origin.x.raw(); };
    const auto top = [](const core::Rect& r) { return r.origin.y.raw(); };
    const auto right = [](const core::Rect& r) { return r.origin.x.raw() + r.extent.w.raw(); };
    const auto bottom = [](const core::Rect& r) { return r.origin.y.raw() + r.extent.h.raw(); };
    const auto min_of = [&](auto f) { return f(*std::ranges::min_element(cards, {}, f)); };
    const auto max_of = [&](auto f) { return f(*std::ranges::max_element(cards, {}, f)); };
    return core::Rect::from_corners({core::X{min_of(left)}, core::Y{min_of(top)}},
                                    {core::X{max_of(right)}, core::Y{max_of(bottom)}});
}

} // namespace

float clamp_zoom(float z) { return std::clamp(z, min_zoom, max_zoom); }

BoardView zoomed_about(BoardView v, core::Offset pointer, float zoom) {
    const float z = clamp_zoom(zoom);
    const float shift = 1.f / z - 1.f / v.zoom;
    return {v.pan + core::Offset{core::DX{pointer.dx.raw() * shift}, core::DY{pointer.dy.raw() * shift}}, z};
}

float wheel_zoom(float zoom, float notches) { return clamp_zoom(zoom * std::pow(zoom_per_notch, notches)); }

BoardView fit_all(std::span<const core::Rect> cards, core::Size size, float margin) {
    if (cards.empty()) return {};
    const core::Rect box = bounds(cards);
    const float fit_w = box.extent.w.raw() > 0.f ? (size.w.raw() - 2 * margin) / box.extent.w.raw() : 1.f;
    const float fit_h = box.extent.h.raw() > 0.f ? (size.h.raw() - 2 * margin) / box.extent.h.raw() : 1.f;
    const float zoom = clamp_zoom(std::min({1.f, fit_w, fit_h}));
    const core::Point centre = box.center();
    return {core::Point{core::X{size.w.raw() / (2 * zoom) - centre.x.raw()},
                        core::Y{size.h.raw() / (2 * zoom) - centre.y.raw()}},
            zoom};
}

} // namespace prism::nodes
