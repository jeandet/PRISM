#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/frame.hpp>
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
    // `clip`: what the scroll views around this one leave visible. The overlay itself is not
    // clipped, so the thumb and its track are cut to it here (both are plain rectangles); a
    // part outside is neither drawn nor grabbable.
    inline void emit_thumb(SceneSnapshot& snap, WidgetId id, Rect viewport,
                           Height content_h, DY scroll_offset, const Theme& theme,
                           std::optional<Rect> clip = std::nullopt) {
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
        Rect track_rect{Point{track_x, viewport.origin.y}, Size{Width{track_inset}, viewport_h}};
        auto visible = [&](Rect r) { return clip ? clip->intersect(r) : r; };
        auto empty = [](Rect r) { return r.extent.w.raw() <= 0.f || r.extent.h.raw() <= 0.f; };
        thumb_rect = visible(thumb_rect);
        track_rect = visible(track_rect);
        if (empty(track_rect)) return;
        if (!empty(thumb_rect)) snap.overlay.filled_rect(thumb_rect, theme.scrollbar_thumb);
        snap.overlay_geometry.push_back({id, thumb_rect});   // [0]: the thumb, even if cut away
        snap.overlay_geometry.push_back({id, track_rect});
        snap.scrollbar_geometry.push_back({id, track_rect});
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
    enum class Kind { Leaf, Row, Column, Spacer, Canvas, Scroll, VirtualList, Table, Tabs, Handle, Layers, Board } kind = Kind::Leaf;
    std::vector<Rect> layer_rects;   // Kind::Layers: where children after the first lie, in its coordinates
    std::vector<Point> board_points;   // Kind::Board: where children after the first lie, in its coordinates
    BoardDraw board_under;   // Kind::Board: drawn above the first child, below the others
    BoardDraw board_over;    // Kind::Board: drawn above all its children
    DY scroll_offset{0};        // only for Kind::Scroll and VirtualList
    Height scroll_content_h{0}; // total content height, for scrollbar rendering
    size_t vlist_visible_start = 0;  // first materialized item index (VirtualList only)
    Height vlist_item_height{0};     // uniform item height (VirtualList only)
    DX table_scroll_x{0};
    size_t table_column_count = 0;
    Height table_header_h{0};
    std::vector<float> split_sizes;  // main-axis size per pane; meaningful only for Kind::Row/Column when split mode is engaged
    const Theme* theme = &layout_detail::layout_default_theme;
    float spacer_size = 0.f;   // Kind::Spacer: 0 stretches, more is a fixed gap
    std::optional<float> canvas_min_width;   // only meaningful for Kind::Canvas
    std::optional<float> canvas_min_height;  // only meaningful for Kind::Canvas
    // Set by prism::app::widget_layout_detail::build_layout() for Leaf/Canvas/Handle nodes -- lets
    // layout_flatten() read/update that widget's snapshot-assembly cache directly,
    // without threading a WidgetId->WidgetNode* lookup through this free function.
    WidgetNode* widget = nullptr;
    // The widget node this one was built from, for every kind (containers too): the passes
    // after layout reach its state through it, not through an id lookup per node.
    WidgetNode* source = nullptr;
    std::optional<Framing> frame;   // Row/Column: drawn around the children, which it insets
    // Set once measured along this axis; a node kept from the last publish keeps it, so its
    // subtree is not measured again.
    std::optional<LayoutAxis> measured_axis;
    // Moved over whole from the last publish: nothing in it changed. Arranged at the same rect,
    // it comes out as it was, so it is not arranged again.
    bool kept = false;
};

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis);
inline void layout_arrange(LayoutNode& node, Rect available);

// What a node draws: its widget's own list, read in place, so building the layout tree each
// publish copies no draw commands; or the node's list, for a node built without a widget.
inline const DrawList& content_draws(const LayoutNode& node) {
    return node.widget ? node.widget->draws : node.draws;
}

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

// A framed container needs its content's size plus the frame's insets.
inline void add_frame_insets(LayoutNode& node, LayoutAxis parent_axis) {
    if (!node.frame) return;
    auto in = frame_insets(*node.frame);
    float across = in.left + in.right, down = in.top + in.bottom;
    bool horiz = parent_axis == LayoutAxis::Horizontal;
    node.hint.preferred += horiz ? across : down;
    node.hint.cross += horiz ? down : across;
}

// The frame's drawing, and the area left inside it for the children.
inline Rect apply_frame(LayoutNode& node, Rect available) {
    if (!node.frame || !node.theme) return available;
    node.draws = frame_draws(*node.frame, available.extent, *node.theme);
    auto in = frame_insets(*node.frame);
    return Rect{Point{available.origin.x + DX{in.left}, available.origin.y + DY{in.top}},
                Size{Width{std::max(0.f, available.extent.w.raw() - in.left - in.right)},
                     Height{std::max(0.f, available.extent.h.raw() - in.top - in.bottom)}}};
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

namespace layout_detail {
inline void measure_node(LayoutNode& node, LayoutAxis parent_axis);
inline Rect layer_rect(const LayoutNode& node, std::size_t i, Rect area);
inline Size natural_size(const SizeHint& vertical);
inline Rect card_rect(const LayoutNode& board, std::size_t i, Rect area);
}

// What a child measured down a column takes when not stretched: its width across, its height down.
inline Size layout_detail::natural_size(const SizeHint& vertical) {
    return {Width{vertical.cross}, Height{vertical.preferred}};
}

// A board's card i (its child i + 1) in the window: at its point, at its natural size, not clamped.
inline Rect layout_detail::card_rect(const LayoutNode& board, std::size_t i, Rect area) {
    const Point p = i < board.board_points.size() ? board.board_points[i] : Point{X{0}, Y{0}};
    return {area.origin + Offset{DX{p.x.raw()}, DY{p.y.raw()}}, natural_size(board.children[i + 1].hint)};
}

// A layer's rect in the window: its place, kept inside the area (cut down if bigger).
inline Rect layout_detail::layer_rect(const LayoutNode& node, std::size_t i, Rect area) {
    const Rect r = i < node.layer_rects.size() ? node.layer_rects[i] : Rect{Point{X{0}, Y{0}}, Size{Width{320}, Height{240}}};
    const float w = std::min(r.extent.w.raw(), area.extent.w.raw()), h = std::min(r.extent.h.raw(), area.extent.h.raw());
    const float x = std::clamp(r.origin.x.raw(), 0.f, area.extent.w.raw() - w);
    const float y = std::clamp(r.origin.y.raw(), 0.f, area.extent.h.raw() - h);
    return {Point{area.origin.x + DX{x}, area.origin.y + DY{y}}, Size{Width{w}, Height{h}}};
}

inline void layout_measure(LayoutNode& node, LayoutAxis parent_axis) {
    if (node.measured_axis == parent_axis) return;   // kept from the last publish, unchanged
    layout_detail::measure_node(node, parent_axis);
    node.measured_axis = parent_axis;
}

inline void layout_detail::measure_node(LayoutNode& node, LayoutAxis parent_axis) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        // A stretchy spacer stretches along its row or column only: one at the end of a menu
        // bar must not make the bar as tall as the window.
        node.hint = node.spacer_size > 0.f
            ? SizeHint{.preferred = node.spacer_size, .expand = false}
            : SizeHint{.preferred = 0, .expand = true, .expand_axis = layout_detail::axis_flag(parent_axis)};
        return;
    case LayoutNode::Kind::Canvas: {
        bool horiz = (parent_axis == LayoutAxis::Horizontal);
        std::optional<float> min_size = horiz ? node.canvas_min_width : node.canvas_min_height;
        std::optional<float> min_across = horiz ? node.canvas_min_height : node.canvas_min_width;
        node.hint = {.preferred = min_size.value_or(0), .cross = min_across.value_or(0), .expand = !min_size};
        return;
    }
    case LayoutNode::Kind::Leaf: {
        auto bb = content_draws(node).bounding_box();
        bool horiz = (parent_axis == LayoutAxis::Horizontal);
        node.hint.preferred = horiz ? node.intrinsic_width.value_or(bb.extent.w.raw()) : bb.extent.h.raw();
        // Across a column too, a widget is as wide as it says: what it drew may be the default
        // width it was first recorded at, before it had a size.
        node.hint.cross     = horiz ? bb.extent.h.raw() : node.intrinsic_width.value_or(bb.extent.w.raw());
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
        layout_detail::add_frame_insets(node, parent_axis);
        return;
    case LayoutNode::Kind::Column:
        layout_detail::measure_linear(node, LayoutAxis::Vertical, parent_axis);
        layout_detail::add_frame_insets(node, parent_axis);
        return;
    case LayoutNode::Kind::Scroll:
    case LayoutNode::Kind::VirtualList:
    case LayoutNode::Kind::Table:
        layout_detail::measure_scrollable(node);
        return;
    case LayoutNode::Kind::Layers:   // as big as what it lies over; the cards take what they say
        for (std::size_t i = 0; i < node.children.size(); ++i)
            layout_measure(node.children[i], i == 0 ? parent_axis : LayoutAxis::Vertical);
        node.hint = node.children.empty() ? SizeHint{.expand = true} : node.children[0].hint;
        return;
    case LayoutNode::Kind::Board:   // takes what it is given; the cards take what they need
        for (std::size_t i = 0; i < node.children.size(); ++i)
            layout_measure(node.children[i], i == 0 ? parent_axis : LayoutAxis::Vertical);
        node.hint = SizeHint{.expand = true};
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
    if (node.kept && node.allocated == available) return;
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

    if (node.kind == LayoutNode::Kind::Layers) {
        for (std::size_t i = 0; i < node.children.size(); ++i)
            layout_arrange(node.children[i], i == 0 ? available : layout_detail::layer_rect(node, i - 1, available));
        return;
    }

    if (node.kind == LayoutNode::Kind::Board) {
        for (std::size_t i = 0; i < node.children.size(); ++i)
            layout_arrange(node.children[i], i == 0 ? available : layout_detail::card_rect(node, i - 1, available));
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

    available = layout_detail::apply_frame(node, available);
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
// definition for the `clip` contract.
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip = std::nullopt);

namespace layout_detail {

inline bool lies_within(Rect inner, Rect outer) {
    return inner.origin.x >= outer.origin.x && inner.origin.y >= outer.origin.y
        && inner.origin.x + DX{inner.extent.w.raw()} <= outer.origin.x + DX{outer.extent.w.raw()}
        && inner.origin.y + DY{inner.extent.h.raw()} <= outer.origin.y + DY{outer.extent.h.raw()};
}

// The visible part of `inner` given what the enclosing scroll views leave visible. Wholly
// visible, it is `inner` itself: an intersection would round its extent.
inline Rect narrow(std::optional<Rect> clip, Rect inner) {
    return !clip || lies_within(inner, *clip) ? inner : clip->intersect(inner);
}

// A geometry entry: where the widget is, and the part of it `clip` leaves to clicks.
inline void push_geometry(SceneSnapshot& snap, WidgetId id, Rect rect, std::optional<Rect> clip) {
    snap.geometry.push_back({id, rect});
    snap.visible.push_back(narrow(clip, rect));
}

inline void push_entry(SceneSnapshot& snap, WidgetId id, Rect rect, DrawList dl,
                       std::optional<Rect> clip = std::nullopt) {
    push_geometry(snap, id, rect, clip);
    snap.draw_lists.push_back(std::make_shared<DrawList>(std::move(dl)));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));
}

inline void flatten_table(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip) {
    auto vp = node.allocated;
    Height item_h = node.vlist_item_height.raw() > 0 ? node.vlist_item_height : Height{24.f};
    Height header_h = node.table_header_h.raw() > 0 ? node.table_header_h : item_h;

    push_geometry(snap, node.id, vp, clip);

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
    push_entry(snap, 0, body_rect, std::move(body_clip));

    // Flatten visible children with scroll offset only
    // (header offset already applied during layout_arrange)
    DY scroll_dy = node.scroll_offset;
    DY neg_scroll{-scroll_dy.raw()};
    for (auto& child : node.children) {
        layout_detail::offset_subtree_y(child, neg_scroll);
        layout_flatten(child, snap, narrow(clip, body_rect));
        layout_detail::offset_subtree_y(child, DY{scroll_dy.raw()});
    }

    DrawList body_clip_pop;
    body_clip_pop.clip_pop();
    push_entry(snap, 0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}, std::move(body_clip_pop));

    scrollbar::emit_thumb(snap, node.id, body_rect, node.scroll_content_h, scroll_dy, *node.theme, clip);
}

inline void flatten_scrollable(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip) {
    // ClipPush for the viewport
    DrawList clip_dl;
    clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
    push_entry(snap, node.id, node.allocated, std::move(clip_dl), clip);

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
        layout_flatten(child, snap, narrow(clip, node.allocated));
        DY restore{scroll_dy.raw()};
        layout_detail::offset_subtree_y(child, restore);
    }

    // ClipPop
    DrawList clip_pop_dl;
    clip_pop_dl.clip_pop();
    push_entry(snap, 0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}, std::move(clip_pop_dl));

    scrollbar::emit_thumb(snap, node.id, node.allocated, node.scroll_content_h,
                          node.scroll_offset, *node.theme, clip);
}

// simplify: a depth-first walk per call; an id-to-rect map built once per flatten if boards
// with many cards and wires show up in profiles.
inline std::optional<Rect> find_rect(const LayoutNode& node, WidgetId id) {
    if (node.id == id) return node.allocated;
    for (auto& child : node.children)
        if (auto r = find_rect(child, id)) return r;
    return std::nullopt;
}

inline std::optional<Rect> board_rect_of(const LayoutNode& board, WidgetId id) {
    for (auto& child : board.children)
        if (auto r = find_rect(child, id))
            return Rect{r->origin - Offset{DX{board.allocated.origin.x.raw()}, DY{board.allocated.origin.y.raw()}}, r->extent};
    return std::nullopt;
}

// Hit testing skips id 0, so a board's drawings never hide the cards under them.
inline void flatten_board_draw(const LayoutNode& board, const BoardDraw& draw, SceneSnapshot& snap) {
    if (!draw) return;
    DrawList dl;
    draw(dl, [&board](WidgetId id) { return board_rect_of(board, id); });
    translate_draw_list(dl, DX{board.allocated.origin.x.raw()}, DY{board.allocated.origin.y.raw()});
    push_entry(snap, 0, board.allocated, std::move(dl));
}

inline void flatten_node_draws(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip) {
    const bool clipped_by_ancestor = clip.has_value();
    // A popup anchor (context menu, dialog) draws nothing in place, but routing needs its
    // origin: its popup is stored in its own coordinates. It gets a zero-size rect there,
    // which nothing can hit.
    const DrawList& draws = content_draws(node);
    bool anchor_only = draws.empty() && node.widget && node.widget->popup_anchor;
    if (draws.empty() && node.kind != LayoutNode::Kind::Canvas && !anchor_only) return;

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
        // Canvas nodes fill their allocation; leaf widgets use drawn content bounds
        // (bounding_box() moves with the draws, clips included, so it is measured before).
        Rect drawn = draws.bounding_box();
        hit_rect = node.kind == LayoutNode::Kind::Canvas ? node.allocated
                 : anchor_only ? Rect{node.allocated.origin, Size{Width{0}, Height{0}}}
                               : Rect{drawn.origin + Offset{dx, dy}, drawn.extent};

        // Copied here, for the widgets that are on screen, not when the layout tree is built.
        auto clipped = std::make_shared<DrawList>();
        clipped->commands.reserve(draws.commands.size() + 2);
        clipped->clip_push(node.allocated.origin, node.allocated.extent);
        for (auto cmd : draws.commands) {
            layout_detail::translate_cmd(cmd, dx, dy);
            clipped->commands.push_back(std::move(cmd));
        }
        clipped->clip_pop();

        if (!clipped_by_ancestor && node.widget) {
            node.widget->cached_screen_rect = node.allocated;
            node.widget->cached_hit_rect = hit_rect;
            node.widget->cached_snapshot_draws = clipped;
        }
        entry = std::move(clipped);
    }

    push_geometry(snap, node.id, hit_rect, clip);
    snap.draw_lists.push_back(std::move(entry));
    snap.z_order.push_back(static_cast<uint32_t>(snap.geometry.size() - 1));
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

// A card wholly outside is not flattened: nothing of it would show.
inline bool shows_in(Rect r, Rect visible) {
    const Rect seen = r.intersect(visible);
    return seen.extent.w.raw() > 0 && seen.extent.h.raw() > 0;
}

inline void flatten_board(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip) {
    const Rect inside = narrow(clip, node.allocated);
    DrawList clip_dl;
    clip_dl.clip_push(node.allocated.origin, node.allocated.extent);
    push_entry(snap, node.id, node.allocated, std::move(clip_dl), clip);
    if (!node.children.empty()) layout_flatten(node.children[0], snap, inside);
    flatten_board_draw(node, node.board_under, snap);
    for (std::size_t i = 1; i < node.children.size(); ++i)
        if (shows_in(node.children[i].allocated, inside)) layout_flatten(node.children[i], snap, inside);
    flatten_board_draw(node, node.board_over, snap);
    DrawList pop_dl;
    pop_dl.clip_pop();
    push_entry(snap, 0, Rect{Point{X{0}, Y{0}}, Size{Width{0}, Height{0}}}, std::move(pop_dl));
}

} // namespace layout_detail

// `clip`: set when some ancestor (Scroll/VirtualList/Table/Board) wraps this node in a
// ClipPush/ClipPop pair; it is the part those ancestors leave visible (their viewports,
// intersected). Draw lists are clipped by the ClipPush; hit areas (SceneSnapshot::visible) and
// scrollbar thumbs, which go to the unclipped overlay, are cut to it. Leaf/Canvas DrawLists are only cached and shared
// across frames (see LayoutNode::widget below) when it is unset -- see the comment at
// that cache check for why.
inline void layout_flatten(LayoutNode& node, SceneSnapshot& snap, std::optional<Rect> clip) {
    switch (node.kind) {
    case LayoutNode::Kind::Spacer:
        return;
    case LayoutNode::Kind::Tabs:
        for (auto& child : node.children)
            layout_flatten(child, snap, clip);
        return;
    case LayoutNode::Kind::Table:
        layout_detail::flatten_table(node, snap, clip);
        return;
    case LayoutNode::Kind::Scroll:
    case LayoutNode::Kind::VirtualList:
        layout_detail::flatten_scrollable(node, snap, clip);
        return;
    case LayoutNode::Kind::Board:
        layout_detail::flatten_board(node, snap, clip);
        return;
    default:
        break;
    }

    layout_detail::flatten_node_draws(node, snap, clip);
    layout_detail::flatten_overlays(node, snap);
    if (node.widget && node.widget->inner_scroll && node.theme) {
        const auto& is = *node.widget->inner_scroll;
        Rect viewport{Point{node.allocated.origin.x + DX{is.viewport.origin.x.raw()},
                            node.allocated.origin.y + DY{is.viewport.origin.y.raw()}}, is.viewport.extent};
        scrollbar::emit_thumb(snap, node.id, viewport, is.content_h, is.offset, *node.theme, clip);
    }

    for (auto& child : node.children) {
        layout_flatten(child, snap, clip);
    }
}

} // namespace prism::ui
