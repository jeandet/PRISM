#pragma once

#include <prism/app/widget_tree_traversal.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

// Dragging a scrollbar thumb or a split handle: what the drag remembers from the press, and
// where the pointer puts the scroll offset or the panes. Pure arithmetic; WidgetTree holds the
// drag in progress and applies the results.
namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

struct ScrollbarDrag {
    WidgetId scroll_id = 0;
    Y anchor_y{0};       // mouse Y at drag start (absolute)
    DY anchor_offset{0}; // scroll offset at drag start
    Height viewport_h{0};
    Height content_h{0};
    Height thumb_h{0};
    float scale = 1.f;   // the scroll view's zoom (a zoomed board's card): the mouse moves in window px
};

struct SplitDrag {
    WidgetId container_id = 0;
    size_t handle_index = 0;
    float anchor = 0.f;
    float orig_before = 0.f;
    float orig_after = 0.f;
};

// The scroll offset with the pointer at `mouse_y`; none when the thumb fills its track.
inline std::optional<DY> scrollbar_drag_offset(const ScrollbarDrag& d, Y mouse_y) {
    Height track_range = d.viewport_h - d.thumb_h;
    if (track_range.raw() <= 0) return std::nullopt;
    DY max_off = max_scroll(d.content_h, d.viewport_h);
    DY dy_pixels{(mouse_y - d.anchor_y).raw() / d.scale};
    float new_offset = std::clamp(
        d.anchor_offset.raw() + dy_pixels.raw() * max_off.raw() / track_range.raw(),
        0.f, max_off.raw());
    return DY{new_offset};
}

// The container's split state; on its first drag, the pane sizes come from the current layout.
inline SplitState& engage_split(WidgetNode& container_wn) {
    auto& ss = container_wn.get_or_create<SplitState>();
    bool vertical = (container_wn.layout_kind == LayoutKind::Column);
    if (!ss.engaged) {
        ss.pane_sizes.clear();
        for (auto& child : container_wn.children) {
            if (child.layout_kind == LayoutKind::Handle) continue;
            ss.pane_sizes.push_back(vertical ? child.arranged_extent.h.raw()
                                              : child.arranged_extent.w.raw());
        }
        ss.engaged = true;
    }
    return ss;
}

// The sizes of the two panes around the dragged handle with the pointer at `pos`; neither
// goes below splitter::min_pane_size_px.
inline std::pair<float, float> split_drag_sizes(const SplitDrag& d, float pos) {
    float delta = pos - d.anchor;
    float before = d.orig_before + delta;
    float after = d.orig_after - delta;
    if (before < splitter::min_pane_size_px) {
        after -= (splitter::min_pane_size_px - before);
        before = splitter::min_pane_size_px;
    }
    if (after < splitter::min_pane_size_px) {
        before -= (splitter::min_pane_size_px - after);
        after = splitter::min_pane_size_px;
    }
    return {before, after};
}

} // namespace prism::app::widget_detail
