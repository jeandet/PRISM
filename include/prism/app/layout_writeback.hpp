#pragma once

#include <prism/app/widget_tree_layout.hpp>
#include <prism/app/widget_tree_traversal.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <any>
#include <cmath>
#include <utility>
#include <vector>

// After a layout pass: what it decided, written back into the widgets (scroll and split
// state, window position and size, re-records at a new size) and the drop areas it found.
namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

// A following list (VirtualListState::follow_tail) that showed its bottom before rows were
// appended moves to the new bottom. Returns whether it moved: its rows are for the old offset.
inline bool keep_at_tail(VirtualListState& vls) {
    DY bottom = max_scroll(Height{static_cast<float>(vls.item_count.raw()) * vls.item_height.raw()}, vls.viewport_h);
    bool following = vls.scroll_offset.raw() >= vls.tail_offset.raw() - 1.f;
    vls.tail_offset = bottom;
    if (!following || vls.scroll_offset.raw() == bottom.raw()) return false;
    vls.scroll_offset = bottom;
    return true;
}

// Returns whether a virtual list's or table's viewport changed height, or a following list
// moved: its rows were materialized for the old one.
inline bool update_scroll_state_of(LayoutNode& layout_node, WidgetNode& node) {
    WidgetNode* wn = &node;
    bool viewport_moved = false;
    if (layout_node.kind == LayoutNode::Kind::Scroll) {
        auto& ss = ensure_scroll_state(*wn);
        ss.viewport_h = layout_node.allocated.extent.h;
        ss.viewport_w = layout_node.allocated.extent.w;
        ss.content_h = layout_node.scroll_content_h;
    }
    if (layout_node.kind == LayoutNode::Kind::VirtualList)
        if (auto* vls = get_vlist_state(*wn)) {
            viewport_moved = vls->viewport_h != layout_node.allocated.extent.h;
            vls->viewport_h = layout_node.allocated.extent.h;
            if (vls->follow_tail) viewport_moved |= keep_at_tail(*vls);
        }
    if (layout_node.kind == LayoutNode::Kind::Table) {
        if (auto* ts = get_table_state(*wn)) {
            bool was_zero = ts->viewport_w.raw() == 0.f;
            Height h{layout_node.allocated.extent.h.raw() - ts->row_height.raw()};
            viewport_moved = ts->viewport_h != h;
            ts->viewport_h = h;
            ts->viewport_w = layout_node.allocated.extent.w;
            if (was_zero && ts->viewport_w.raw() > 0.f) wn->dirty = true;
        }
    }
    if (auto sv = get_scroll_view(*wn)) {
        DY max_off = max_scroll(sv->content_h, sv->viewport_h);
        sv->offset = DY{std::clamp(sv->offset.raw(), 0.f, max_off.raw())};
        layout_node.scroll_offset = sv->offset;
        if (sv->show_ticks > 0) sv->show_ticks--;
    }
    return viewport_moved;
}

// A kept subtree holds no scroll view (link_layout_cache), here and in the passes below.
inline bool update_scroll_state(LayoutNode& layout_node) {
    if (layout_node.kept) return false;
    bool moved = layout_node.source && update_scroll_state_of(layout_node, *layout_node.source);
    for (auto& child : layout_node.children)
        moved |= update_scroll_state(child);
    return moved;
}

// The screen area of every container that takes drops (a dock group): a drop on its empty
// space, where no widget is hit, still finds it. Moved up by the scroll areas above it.
inline void collect_drop_areas(const LayoutNode& n, DY scrolled, std::vector<std::pair<WidgetId, Rect>>& out) {
    if (n.kept) return;
    if (n.kind != LayoutNode::Kind::Leaf && n.source && n.source->drag.accepts)
        out.push_back({n.id, Rect{Point{n.allocated.origin.x, n.allocated.origin.y - scrolled}, n.allocated.extent}});
    for (auto& c : n.children) collect_drop_areas(c, scrolled + n.scroll_offset, out);
}

inline void update_split_state(LayoutNode& layout_node) {
    if (layout_node.kept) return;
    bool is_split_container = (layout_node.kind == LayoutNode::Kind::Row
                             || layout_node.kind == LayoutNode::Kind::Column)
                             && !layout_node.split_sizes.empty();
    if (is_split_container && layout_node.source) {
        {
            if (auto* ss = std::any_cast<SplitState>(&layout_node.source->edit_state); ss && ss->engaged) {
                bool vertical = (layout_node.kind == LayoutNode::Kind::Column);
                float available = vertical ? layout_node.allocated.extent.h.raw()
                                            : layout_node.allocated.extent.w.raw();
                float handle_total = 0.f;
                for (auto& child : layout_node.children)
                    if (child.kind == LayoutNode::Kind::Handle)
                        handle_total += splitter::thickness_px;
                float pane_total = 0.f;
                for (float sz : ss->pane_sizes) pane_total += sz;
                float target = available - handle_total;
                // A split nested in another starts out squeezed (the outer one's sizes are still
                // fractions on the first pass): scaling to a target below zero would leave
                // negative sizes that no later pass corrects.
                if (pane_total > 0.f && target > 0.f && std::abs(target - pane_total) > 0.5f) {
                    float scale = target / pane_total;
                    for (auto& sz : ss->pane_sizes) sz *= scale;
                }
            }
        }
    }
    for (auto& child : layout_node.children)
        update_split_state(child);
}

// Returns whether a widget re-recorded at its new size changed height (a label that wraps):
// then the layout it was placed by is stale, and the caller lays out again.
// `scrolled`: how far the scroll views around this node are scrolled; layout positions are in
// their content, the absolute ones are in the window.
inline bool update_canvas_bounds(LayoutNode& layout_node, Size viewport, DY scrolled = DY{0}) {
    // Propagate absolute position and the window size to all indexed widget nodes
    bool window_changed = false;
    if (layout_node.id != 0 && layout_node.source) {
        {
            auto& n = *layout_node.source;
            Y on_screen_y = layout_node.allocated.origin.y - scrolled;
            window_changed = n.viewport_height != viewport.h || n.viewport_width != viewport.w
                          || n.absolute_x != layout_node.allocated.origin.x
                          || n.absolute_y != on_screen_y;
            bool in_place = !window_changed && n.arranged_extent == layout_node.allocated.extent;
            if (layout_node.kept && in_place) return false;   // and so is all of it, as written last time
            n.absolute_x = layout_node.allocated.origin.x;
            n.absolute_y = on_screen_y;
            n.viewport_height = viewport.h;
            n.viewport_width = viewport.w;
            n.arranged_extent = layout_node.allocated.extent;
        }
    }

    // Re-record leaf/canvas/handle widgets after layout so delegates can use their
    // allocated size instead of a pre-layout guess -- but only when that size actually
    // differs from wn->canvas_bounds (the size wn->draws was last produced at, whether
    // by refresh_dirty()'s pre-layout call this same build_snapshot(), or by this same
    // re-record last frame). No widget here sizes its own record() output from its field
    // value independent of allocated size (bounding_box() never measures text/points by
    // content, see draw_list.hpp), so if the size didn't move, the currently-held draws
    // are still correct -- skipping avoids a second full record() for every leaf/canvas/
    // handle in the window on every publish, including ones untouched by whatever else
    // made the window dirty (see benchmarks/stall_latency.cpp's
    // untouched_sibling_record_calls, and tests/test_record_reuse.cpp).
    if (layout_node.kind == LayoutNode::Kind::Leaf ||
        layout_node.kind == LayoutNode::Kind::Canvas ||
        layout_node.kind == LayoutNode::Kind::Handle) {
        auto* wn = layout_node.widget;   // set by build_layout() for these three kinds
        // layout_node reads wn->draws in place (ui::content_draws), so a re-record here is
        // what layout_flatten() sees; overlay_draws is still a copy and is refreshed.
        // Popup anchors and widgets showing their own overlay (a dropdown's list, a calendar)
        // place it relative to the window, so they also re-record when the window or their
        // position in it changes.
        bool resized = wn && wn->canvas_bounds.extent != layout_node.allocated.extent;
        bool places_popup = wn && (wn->popup_anchor || !wn->overlay_draws.empty());
        if (wn && wn->record && (resized || (window_changed && places_popup))) {
            wn->canvas_bounds = Rect{
                Point{X{0}, Y{0}},
                layout_node.allocated.extent
            };
            float height_before = wn->draws.bounding_box().extent.h.raw();
            wn->record(*wn);
            wn->relayout = true;   // its draws changed: measure it again next time
            layout_node.overlay_draws = wn->overlay_draws;
            return wn->draws.bounding_box().extent.h.raw() != height_before;
        }
        return false;
    }
    bool changed = false;
    for (auto& child : layout_node.children)
        changed |= update_canvas_bounds(child, viewport, scrolled + layout_node.scroll_offset);
    return changed;
}

} // namespace prism::app::widget_detail
