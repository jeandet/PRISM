#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include <vector>

namespace prism::ui {
using namespace prism::render;


namespace scrollbar {
    inline constexpr Width track_width{6.f};
    inline constexpr Width track_inset{8.f};
    inline constexpr Height min_thumb_h{20.f};

    inline Height thumb_height(Height viewport_h, Height content_h) {
        return Height{std::max(min_thumb_h.raw(), viewport_h.raw() * (viewport_h.raw() / content_h.raw()))};
    }

    // Draws the thumb into `snap.overlay` and records hit regions in
    // `snap.overlay_geometry` -- the overlay hit region is what starts a
    // scrollbar drag (see app::widget_detail::route_mouse_button), so both go
    // through this one function. Two entries: the thumb first (tests and
    // callers read [0] as the moving thumb), then the full-height track gutter,
    // so a press anywhere on the track starts a drag for every scrollable kind.
    // No-op when the content fits the viewport.
    // simplify: the overlay is unclipped, so a scrollbar inside another scroll view that is
    // scrolled out of sight still draws (a TextArea or a nested Scroll in a ScrollArea).
    // Upgrade: carry the ancestors' clip rect down layout_flatten and clip or skip the thumb.
    inline void emit_thumb(SceneSnapshot& snap, WidgetId id, Rect viewport,
                           Height content_h, DY scroll_offset, const Theme& theme) {
        Height viewport_h = viewport.extent.h;
        if (content_h.raw() <= viewport_h.raw()) return;
        Height thumb_h = thumb_height(viewport_h, content_h);
        float max_scroll = content_h.raw() - viewport_h.raw();
        float thumb_y = max_scroll > 0
            ? scroll_offset.raw() * (viewport_h.raw() - thumb_h.raw()) / max_scroll
            : 0.f;
        X track_x{viewport.origin.x.raw() + viewport.extent.w.raw() - track_inset.raw()};
        Rect thumb_rect{
            Point{track_x, viewport.origin.y + DY{thumb_y}},
            Size{Width{track_width}, thumb_h}};
        snap.overlay.filled_rect(thumb_rect, theme.scrollbar_thumb);
        snap.overlay_geometry.push_back({id, thumb_rect});
        snap.overlay_geometry.push_back({id, Rect{Point{track_x, viewport.origin.y},
                                                  Size{Width{track_inset}, viewport_h}}});
    }
}

namespace splitter {
    inline constexpr float thickness_px = 6.f;
    inline constexpr float min_pane_size_px = 24.f;
}

enum class LayoutAxis { Horizontal, Vertical };

struct SizeHint {
    float preferred = 0;
    float min = 0;
    float max = std::numeric_limits<float>::max();
    float cross = 0;
    bool expand = false;
    ExpandAxis expand_axis = ExpandAxis::None;
};

namespace layout_detail {
    inline const Theme layout_default_theme = default_theme();
}

struct LayoutNode {
    WidgetId id = 0;
    SizeHint hint;
    // Width the content needs when laid out in a row (text plus padding); unset = derive it from
    // the draws, as before. Drawn size can't serve: widgets draw at their last allocation.
    std::optional<float> intrinsic_width;
    Rect allocated{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}};
    DrawList draws;
    DrawList overlay_draws;
    std::vector<LayoutNode> children;
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle } kind = Kind::Leaf;
    DY scroll_offset{0};        // only for Kind::Scroll and VirtualList
    Height scroll_content_h{0}; // total content height, for scrollbar rendering
    size_t vlist_visible_start = 0;  // first materialized item index (VirtualList only)
    Height vlist_item_height{0};     // uniform item height (VirtualList only)
    DX table_scroll_x{0};
    size_t table_column_count = 0;
    Height table_header_h{0};
    std::vector<float> split_sizes;  // main-axis size per pane; meaningful only for Kind::Row/Column when split mode is engaged
    const Theme* theme = &layout_detail::layout_default_theme;
    std::optional<float> canvas_min_width;   // only meaningful for Kind::Canvas
    std::optional<float> canvas_min_height;  // only meaningful for Kind::Canvas
    // Set by prism::app::widget_layout_detail::build_layout() for Leaf/Canvas/Handle nodes -- lets
    // layout_flatten() read/update that widget's snapshot-assembly cache directly,
    // without threading a WidgetId->WidgetNode* lookup through this free function.
    WidgetNode* widget = nullptr;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis);
inline void layout_arrange(LayoutNode& node, Rect available);

namespace layout_detail {

inline ExpandAxis axis_flag(LayoutAxis a) {
    return a == LayoutAxis::Horizontal ? ExpandAxis::Horizontal : ExpandAxis::Vertical;
}

// Whether a measured child wants extra space along `axis`. ExpandAxis::None with expand set is
// the legacy "expand" (plots, canvases, lists): both axes.
inline bool expands_along(const SizeHint& h, LayoutAxis axis) {
    if (h.expand_axis == ExpandAxis::None) return h.expand;
    return h.expand_axis == ExpandAxis::Both || h.expand_axis == axis_flag(axis);
}

inline void measure_linear(LayoutNode& node, LayoutAxis own_axis, LayoutAxis parent_axis) {
    float sum = 0, max_cross = 0;
    bool has_expander = false;        // some child expands along this container's own axis
    bool has_cross_expander = false;  // some child expands across it
    LayoutAxis cross_axis = own_axis == LayoutAxis::Horizontal ? LayoutAxis::Vertical : LayoutAxis::Horizontal;
    size_t pane_index = 0;
    for (auto& child : node.children) layout_measure(child, own_axis);
    // A splitter divides space: if no pane expands on its own, they all share it (until a drag
    // fixes their sizes) instead of sitting at their content size with the rest unused.
    // Panes also never go below the size a drag can produce, so engaging a split never snaps them.
    auto is_handle = [](const LayoutNode& c) { return c.kind == LayoutNode::Kind::Handle; };
    if (std::ranges::any_of(node.children, is_handle)) {
        bool none_expand = std::ranges::none_of(node.children, [](const LayoutNode& c) { return c.hint.expand; });
        for (auto& child : node.children) {
            if (is_handle(child)) continue;
            child.hint.preferred = std::max(child.hint.preferred, splitter::min_pane_size_px);
            if (none_expand && node.split_sizes.empty()) child.hint.expand = true;
        }
    }
    for (auto& child : node.children) {
        // Read the child's expand intent BEFORE the split override below clears it:
        // engaging split mode fixes pane sizes along this container's own axis, but says
        // nothing about how the container itself should size in its parent -- a row with
        // an expanding pane (e.g. a tree's VirtualList) must keep claiming expand-share
        // from its parent column after the drag, or it falls back to its cross-hint and
        // starves every other expander to zero height.
        if (child.hint.expand) has_expander = true;
        if (expands_along(child.hint, cross_axis)) has_cross_expander = true;
        if (!node.split_sizes.empty() && child.kind != LayoutNode::Kind::Handle) {
            if (pane_index < node.split_sizes.size()) {
                child.hint.preferred = node.split_sizes[pane_index];
                child.hint.expand = false;
            }
            ++pane_index;
        }
        sum += child.hint.preferred;
        max_cross = std::max(max_cross, child.hint.cross);
    }
    bool aligned = (own_axis == parent_axis);
    node.hint.preferred = aligned ? sum : max_cross;
    node.hint.cross = aligned ? max_cross : sum;
    // Expansion is per axis: a row whose text field fills it horizontally must not also claim
    // spare height from its parent column.
    node.hint.expand = aligned ? has_expander : has_cross_expander;
    node.hint.expand_axis = has_expander && has_cross_expander ? ExpandAxis::Both
                          : has_expander                       ? axis_flag(own_axis)
                          : has_cross_expander                 ? axis_flag(cross_axis)
                                                               : ExpandAxis::None;
}

inline void measure_scrollable(LayoutNode& node) {
    float max_cross = 0;
    for (auto& child : node.children) {
        layout_measure(child, LayoutAxis::Vertical);
        max_cross = std::max(max_cross, child.hint.cross);
    }
    node.hint.expand = true;
    node.hint.preferred = 0;
    node.hint.cross = max_cross;
}

// Shared body of layout_arrange's Table/VirtualList branches: every child gets the
// full width and a uniform row height, stacked from first_row_top downward.
inline void arrange_uniform_rows(LayoutNode& node, Rect available, Height item_h, Y first_row_top) {
    Y start_y{first_row_top.raw() + static_cast<float>(node.vlist_visible_start) * item_h.raw()};
    for (auto& child : node.children) {
        layout_arrange(child, {
            Point{available.origin.x, start_y},
            Size{available.extent.w, item_h}
        });
        start_y += DY{item_h.raw()};
    }
}

} // namespace layout_detail

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        node.hint = {.preferred = 0, .expand = true};
        return;
    case LayoutNode::Kind::Canvas: {
        bool horiz = (parent_axis == LayoutAxis::Horizontal);
        std::optional<float> min_size = horiz ? node.canvas_min_width : node.canvas_min_height;
        if (min_size.has_value())
            node.hint = {.preferred = *min_size, .expand = false};
        else
            node.hint = {.preferred = 0, .expand = true};
        return;
    }
    case LayoutNode::Kind::Leaf: {
        auto bb = node.draws.bounding_box();
        bool horiz = (parent_axis == LayoutAxis::Horizontal);
        node.hint.preferred = horiz ? node.intrinsic_width.value_or(bb.extent.w.raw()) : bb.extent.h.raw();
        node.hint.cross     = horiz ? bb.extent.h.raw() : bb.extent.w.raw();
        // Directional expand: only expand along the widget's declared axis
        if (node.hint.expand_axis != ExpandAxis::None) {
            bool expands_h = (node.hint.expand_axis == ExpandAxis::Both ||
                              node.hint.expand_axis == ExpandAxis::Horizontal);
            bool expands_v = (node.hint.expand_axis == ExpandAxis::Both ||
                              node.hint.expand_axis == ExpandAxis::Vertical);
            node.hint.expand = horiz ? expands_h : expands_v;
        }
        return;
    }
    case LayoutNode::Kind::Handle:
        node.hint = {.preferred = splitter::thickness_px, .expand = false};
        return;
    case LayoutNode::Kind::Row:
        layout_detail::measure_linear(node, LayoutAxis::Horizontal, parent_axis);
        return;
    case LayoutNode::Kind::Column:
        layout_detail::measure_linear(node, LayoutAxis::Vertical, parent_axis);
        return;
    case LayoutNode::Kind::Scroll:
    case LayoutNode::Kind::VirtualList:
    case LayoutNode::Kind::Table:
        layout_detail::measure_scrollable(node);
        return;
    case LayoutNode::Kind::Tabs: {
        if (node.children.size() >= 1)
            layout_measure(node.children[0], LayoutAxis::Horizontal);
        if (node.children.size() >= 2)
            layout_detail::measure_linear(node.children[1], LayoutAxis::Vertical, parent_axis);
        node.hint.expand = true;
        node.hint.preferred = 0;
        return;
    }
    }
}

inline void layout_arrange(LayoutNode& node, Rect available) {
    node.allocated = available;

    if (node.kind == LayoutNode::Kind::Table) {
        Height item_h = node.vlist_item_height;
        if (item_h.raw() <= 0) item_h = Height{24.f};
        Height header_h = node.table_header_h.raw() > 0 ? node.table_header_h : item_h;
        Y body_top{available.origin.y.raw() + header_h.raw()};
        layout_detail::arrange_uniform_rows(node, available, item_h, body_top);
        return;
    }

    if (node.kind == LayoutNode::Kind::VirtualList) {
        Height item_h = node.vlist_item_height;
        if (item_h.raw() <= 0 && !node.children.empty())
            item_h = Height{node.children[0].hint.preferred};
        layout_detail::arrange_uniform_rows(node, available, item_h, available.origin.y);
        return;
    }

    if (node.kind == LayoutNode::Kind::Scroll) {
        Height viewport_h = available.extent.h;
        DY offset{0};
        for (auto& child : node.children) {
            Height main_size = child.hint.expand
                ? Height{std::max(child.hint.preferred, viewport_h.raw())} : Height{child.hint.preferred};
            layout_arrange(child, {
                Point{available.origin.x, available.origin.y + offset},
                Size{available.extent.w, main_size}
            });
            offset += DY{main_size.raw()};
        }
        node.scroll_content_h = Height{offset.raw()};
        return;
    }

    if (node.kind == LayoutNode::Kind::Tabs) {
        Height bar_h{node.children.size() >= 1 ? node.children[0].hint.cross : 0.f};
        if (node.children.size() >= 1) {
            layout_arrange(node.children[0], {
                available.origin,
                Size{available.extent.w, bar_h}
            });
        }
        if (node.children.size() >= 2) {
            layout_arrange(node.children[1], {
                Point{available.origin.x, available.origin.y + DY{bar_h.raw()}},
                Size{available.extent.w, Height{available.extent.h.raw() - bar_h.raw()}}
            });
        }
        return;
    }

    if (node.children.empty()) return;

    bool horizontal = (node.kind == LayoutNode::Kind::Row);

    // Count expanders and sum preferred sizes
    float total_preferred = 0;
    int expand_count = 0;
    for (auto& child : node.children) {
        if (child.hint.expand)
            ++expand_count;
        else
            total_preferred += child.hint.preferred;
    }

    float remaining = (horizontal ? available.extent.w.raw() : available.extent.h.raw()) - total_preferred;
    float expand_share = (expand_count > 0) ? std::max(0.f, remaining) / expand_count : 0;

    float offset = 0;
    for (auto& child : node.children) {
        float main_size = child.hint.expand ? expand_share : child.hint.preferred;
        Rect child_rect;
        if (horizontal) {
            child_rect = {Point{X{available.origin.x.raw() + offset}, available.origin.y},
                          Size{Width{main_size}, available.extent.h}};
        } else {
            child_rect = {Point{available.origin.x, Y{available.origin.y.raw() + offset}},
                          Size{available.extent.w, Height{main_size}}};
        }
        layout_arrange(child, child_rect);
        offset += main_size;
    }
}

namespace layout_detail {

inline void translate_cmd(DrawCmd& cmd, DX dx, DY dy) {
    std::visit([dx, dy](auto& c) {
        if constexpr (requires { c.rect; }) {
            c.rect.origin.x = X{c.rect.origin.x.raw() + dx.raw()};
            c.rect.origin.y = Y{c.rect.origin.y.raw() + dy.raw()};
        } else if constexpr (requires { c.center; c.radius; }) {
            c.center.x = X{c.center.x.raw() + dx.raw()};
            c.center.y = Y{c.center.y.raw() + dy.raw()};
        } else if constexpr (requires { c.points; }) {
            for (auto& p : c.points) {
                p.x = X{p.x.raw() + dx.raw()};
                p.y = Y{p.y.raw() + dy.raw()};
            }
        } else if constexpr (requires { c.from; c.to; }) {
            c.from.x = X{c.from.x.raw() + dx.raw()};
            c.from.y = Y{c.from.y.raw() + dy.raw()};
            c.to.x = X{c.to.x.raw() + dx.raw()};
            c.to.y = Y{c.to.y.raw() + dy.raw()};
        } else if constexpr (requires { c.origin; }) {
            c.origin.x = X{c.origin.x.raw() + dx.raw()};
            c.origin.y = Y{c.origin.y.raw() + dy.raw()};
        }
    }, cmd);
}

inline void translate_draw_list(DrawList& dl, DX dx, DY dy) {
    for (auto& cmd : dl.commands)
        translate_cmd(cmd, dx, dy);
}

inline void offset_subtree_y(LayoutNode& node, DY dy) {
    node.allocated.origin.y = Y{node.allocated.origin.y.raw() + dy.raw()};
    for (auto& child : node.children)
        offset_subtree_y(child, dy);
}

} // namespace layout_detail

// Forward declaration — the flatten helpers below recurse into it. See the
// definition for the clipped_by_ancestor contract.
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap, bool clipped_by_ancestor = false);

namespace layout_detail {

inline void flatten_table(LayoutNode& node, SceneSnapshot& snap) {
    auto vp = node.allocated;
    Height item_h = node.vlist_item_height.raw() > 0 ? node.vlist_item_height : Height{24.f};
    Height header_h = node.table_header_h.raw() > 0 ? node.table_header_h : item_h;

    snap.geometry.push_back({node.id, vp});

    // Header background (fixed at top, doesn't scroll vertically)
    DrawList header_dl;
    header_dl.clip_push(vp.origin, Size{vp.extent.w, header_h});
    header_dl.filled_rect(
        Rect{vp.origin, Size{vp.extent.w, header_h}},
        node.theme->table_header);
    header_dl.filled_rect(
        Rect{Point{vp.origin.x, vp.origin.y + DY{header_h.raw() - 2.f}},
             Size{vp.extent.w, Height{2.f}}},
        node.theme->table_header_divider);
    // Header cell text
    DX hdr_dx{vp.origin.x.raw()};
    DY hdr_dy{vp.origin.y.raw()};
    for (auto& cmd : node.overlay_draws.commands) {
        auto translated = cmd;
        layout_detail::translate_cmd(translated, hdr_dx, hdr_dy);
        header_dl.commands.push_back(std::move(translated));
    }
    header_dl.clip_pop();
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(header_dl)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));

    // Body region (clipped, scrollable)
    Rect body_rect{
        Point{vp.origin.x, vp.origin.y + DY{header_h.raw()}},
        Size{vp.extent.w, vp.extent.h - header_h}};

    DrawList body_clip;
    body_clip.clip_push(body_rect.origin, body_rect.extent);
    snap.geometry.push_back({0, body_rect});
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(body_clip)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));

    // Flatten visible children with scroll offset only
    // (header offset already applied during layout_arrange)
    DY scroll_dy = node.scroll_offset;
    DY neg_scroll{-scroll_dy.raw()};
    for (auto& child : node.children) {
        layout_detail::offset_subtree_y(child, neg_scroll);
        layout_flatten(child, snap, /*clipped_by_ancestor=*/true);
        layout_detail::offset_subtree_y(child, DY{scroll_dy.raw()});
    }

    DrawList body_clip_pop;
    body_clip_pop.clip_pop();
    snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(body_clip_pop)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));

    scrollbar::emit_thumb(snap, node.id, body_rect, node.scroll_content_h, scroll_dy, *node.theme);
}

inline void flatten_scrollable(LayoutNode& node, SceneSnapshot& snap) {
    // ClipPush for the viewport
    DrawList clip_dl;
    clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
    snap.geometry.push_back({node.id, node.allocated});
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(clip_dl)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));

    // Flatten visible children with scroll offset applied to entire subtree
    DY scroll_dy = node.scroll_offset;
    DY neg_scroll{-scroll_dy.raw()};
    for (auto& child : node.children) {
        Y child_top = child.allocated.origin.y - scroll_dy;
        Y child_bottom = child_top + DY{child.allocated.extent.h.raw()};
        Y vp_top = node.allocated.origin.y;
        Y vp_bottom = vp_top + DY{node.allocated.extent.h.raw()};

        if (child_bottom <= vp_top || child_top >= vp_bottom)
            continue;

        layout_detail::offset_subtree_y(child, neg_scroll);
        layout_flatten(child, snap, /*clipped_by_ancestor=*/true);
        DY restore{scroll_dy.raw()};
        layout_detail::offset_subtree_y(child, restore);
    }

    // ClipPop
    DrawList clip_pop_dl;
    clip_pop_dl.clip_pop();
    snap.geometry.push_back({0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}});
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(clip_pop_dl)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));

    scrollbar::emit_thumb(snap, node.id, node.allocated, node.scroll_content_h,
                          node.scroll_offset, *node.theme);
}

inline void flatten_node_draws(LayoutNode& node, SceneSnapshot& snap, bool clipped_by_ancestor) {
    // A popup anchor (context menu, dialog) draws nothing in place, but routing needs its
    // origin: its popup is stored in its own coordinates. It gets a zero-size rect there,
    // which nothing can hit.
    bool anchor_only = node.draws.empty() && node.widget && node.widget->popup_anchor;
    if (node.draws.empty() && node.kind != LayoutNode::Kind::Canvas && !anchor_only) return;

    // Reuse last frame's already-translated+clipped DrawList when this widget's content
    // didn't change (!dirty) AND it's still at the same screen rect -- a widget can be
    // clean but still relocate (a sibling resized, the window resized, ...), which would
    // make a stale translated copy wrong. Restricted to !clipped_by_ancestor: resolve_clips
    // only ever mutates a ClipPush rect in place when an outer clip is already active on
    // its stack, and that can only happen for entries reached with clipped_by_ancestor
    // true -- so an entry eligible for reuse here is guaranteed to never be mutated after
    // publication, which is what makes sharing it across snapshots (and across the render
    // thread) safe. Rows inside Scroll/VirtualList/Table therefore never get this reuse --
    // simplify: recycled-row identity would need its own invalidation story; revisit if
    // profiling shows scrolled/tabular content dominates snapshot-build cost.
    bool cache_hit = !clipped_by_ancestor && node.widget && !node.widget->dirty
        && node.widget->cached_snapshot_draws
        && node.widget->cached_screen_rect == node.allocated;

    Rect hit_rect;
    std::shared_ptr<const DrawList> entry;
    if (cache_hit) {
        entry = node.widget->cached_snapshot_draws;
        hit_rect = node.widget->cached_hit_rect;
    } else {
        DX dx{node.allocated.origin.x.raw()};
        DY dy{node.allocated.origin.y.raw()};
        layout_detail::translate_draw_list(node.draws, dx, dy);
        // Canvas nodes fill their allocation; leaf widgets use drawn content bounds
        hit_rect = node.kind == LayoutNode::Kind::Canvas ? node.allocated
                 : anchor_only ? Rect{node.allocated.origin, Size{Width{0}, Height{0}}}
                               : node.draws.bounding_box();

        auto clipped = std::make_shared<DrawList>();
        clipped->clip_push(node.allocated.origin, node.allocated.extent);
        clipped->commands.insert(clipped->commands.end(),
            std::make_move_iterator(node.draws.commands.begin()),
            std::make_move_iterator(node.draws.commands.end()));
        clipped->clip_pop();

        if (!clipped_by_ancestor && node.widget) {
            node.widget->cached_screen_rect = node.allocated;
            node.widget->cached_hit_rect = hit_rect;
            node.widget->cached_snapshot_draws = clipped;
        }
        entry = std::move(clipped);
    }

    auto idx = static_cast<uint32_t>(snap.geometry.size());
    snap.geometry.push_back({node.id, hit_rect});
    snap.draw_lists.push_back(std::move(entry));
    snap.z_order.push_back(idx);
}

inline void flatten_overlays(LayoutNode& node, SceneSnapshot& snap) {
    if (node.overlay_draws.empty()) return;
    DX dx{node.allocated.origin.x.raw()};
    DY dy{node.allocated.origin.y.raw()};
    layout_detail::translate_draw_list(node.overlay_draws, dx, dy);
    snap.overlay_geometry.push_back({node.id, node.overlay_draws.bounding_box()});
    for (auto& cmd : node.overlay_draws.commands)
        snap.overlay.commands.push_back(std::move(cmd));
}

} // namespace layout_detail

// `clipped_by_ancestor`: true when some ancestor (Scroll/VirtualList/Table) already wraps
// this node in a ClipPush/ClipPop pair. Leaf/Canvas DrawLists are only cached and shared
// across frames (see LayoutNode::widget below) when this is false -- see the comment at
// that cache check for why.
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap, bool clipped_by_ancestor) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        return;
    case LayoutNode::Kind::Tabs:
        for (auto& child : node.children)
            layout_flatten(child, snap, clipped_by_ancestor);
        return;
    case LayoutNode::Kind::Table:
        layout_detail::flatten_table(node, snap);
        return;
    case LayoutNode::Kind::Scroll:
    case LayoutNode::Kind::VirtualList:
        layout_detail::flatten_scrollable(node, snap);
        return;
    default:
        break;
    }

    layout_detail::flatten_node_draws(node, snap, clipped_by_ancestor);
    layout_detail::flatten_overlays(node, snap);
    if (node.widget && node.widget->inner_scroll && node.theme) {
        const auto& is = *node.widget->inner_scroll;
        Rect viewport{Point{node.allocated.origin.x + DX{is.viewport.origin.x.raw()},
                            node.allocated.origin.y + DY{is.viewport.origin.y.raw()}}, is.viewport.extent};
        scrollbar::emit_thumb(snap, node.id, viewport, is.content_h, is.offset, *node.theme);
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap, clipped_by_ancestor);
    }
}

} // namespace prism::ui
