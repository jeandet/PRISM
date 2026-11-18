#pragma once

#include <prism/core/types.hpp>

#include <span>

namespace prism::nodes {

// How the board shows the graph: a node at `position` lies at board origin + zoom·(position + pan).
struct BoardView {
    core::Point pan{};
    float zoom = 1.f;
    bool operator==(const BoardView&) const = default;
};

inline constexpr float min_zoom = 0.25f;
inline constexpr float max_zoom = 2.f;
inline constexpr float zoom_per_notch = 1.1f;

float clamp_zoom(float z);

// The view at `zoom` (clamped) that keeps the board point under `pointer` (window offset from
// the board's top-left) where it is.
BoardView zoomed_about(BoardView v, core::Offset pointer, float zoom);

// The zoom after `notches` wheel notches (fractional from touchpads; > 0 zooms in): zoom × 1.1^notches, clamped.
float wheel_zoom(float zoom, float notches);

// The view showing every rect (node cards, at natural size, in graph positions) inside a
// board of `size`, `margin` px clear on each side; zoom clamped and at most 1. Empty: {}.
BoardView fit_all(std::span<const core::Rect> cards, core::Size size, float margin = 24.f);

} // namespace prism::nodes
