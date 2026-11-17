#pragma once

#include <prism/ui/layout.hpp>
#include <prism/ui/widget_node.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/text/view.hpp>

#include <any>
#include <memory>
#include <optional>
#include <utility>

namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::ui;

inline ScrollState& ensure_scroll_state(WidgetNode& node) {
    return node.get_or_create<ScrollState>();
}

inline TableState* get_table_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TableState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline VirtualListState* get_vlist_state(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<VirtualListState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

inline TabsState* get_tabs_state_ptr(WidgetNode& node) {
    auto* sp = std::any_cast<std::shared_ptr<TabsState>>(&node.edit_state);
    return sp ? sp->get() : nullptr;
}

struct ScrollView {
    DY& offset;
    Height viewport_h;
    Height content_h;
    uint8_t& show_ticks;
    ScrollEventPolicy event_policy;
};

inline std::optional<ScrollView> get_scroll_view(WidgetNode& node) {
    if (!node.edit_state.has_value()) return std::nullopt;   // most widgets: no state to cast
    if (auto* ss = std::any_cast<ScrollState>(&node.edit_state))
        return ScrollView{ss->offset_y, ss->viewport_h, ss->content_h, ss->show_ticks, ss->event_policy};
    if (auto* vls = get_vlist_state(node)) {
        Height ch{static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
        return ScrollView{vls->scroll_offset, vls->viewport_h, ch, vls->show_ticks, vls->event_policy};
    }
    if (auto* ts = get_table_state(node)) {
        Height ch{static_cast<float>(ts->row_count()) * ts->row_height.raw()};
        return ScrollView{ts->scroll_y, ts->viewport_h, ch, ts->show_ticks, ts->event_policy};
    }
    if (auto* tv = std::any_cast<std::shared_ptr<text::View>>(&node.edit_state))
        return ScrollView{(*tv)->scroll, (*tv)->viewport_h, (*tv)->content_h, (*tv)->show_ticks,
                          ScrollEventPolicy::BubbleAtBounds};
    if (auto* ta = std::any_cast<TextAreaEditState>(&node.edit_state))
        return ScrollView{ta->scroll_y, ta->viewport_h, ta->content_h, ta->show_ticks,
                          ScrollEventPolicy::BubbleAtBounds};
    return std::nullopt;
}

// `keep`: move over the subtrees link_layout_cache() just marked as still holding. Only a
// layout linked right before may say so; any other caller builds everything.
inline void build_layout(WidgetNode& node, LayoutNode& parent, bool keep = false);

// --- Keeping the layout tree between publishes ---
// A widget subtree with nothing changed since the last publish keeps its layout nodes: they are
// moved over, already measured, instead of being built and measured again. Arrange still runs
// over the whole tree, so positions are always fresh.

// Containers whose layout nodes carry state read at build time (scroll offsets, visible rows,
// table columns, the selected tab) are always built again; their children may still be kept.
inline bool keeps_state(const WidgetNode& n) {
    using LK = LayoutKind;
    return n.layout_kind == LK::Scroll || n.layout_kind == LK::VirtualList || n.layout_kind == LK::Table
        || n.layout_kind == LK::Tabs || n.layout_kind == LK::Layers;
}
// A container that hands its children to its parent's layout node (no node of its own).
inline bool passes_through(const WidgetNode& n) {
    using LK = LayoutKind;
    return n.is_container && !keeps_state(n) && n.layout_kind != LK::Row && n.layout_kind != LK::Column;
}
// A splitter rewrites its panes' size hints while measuring, so they are measured afresh.
inline bool splits(const WidgetNode& n) {
    return n.is_container && (std::any_cast<SplitState>(&n.edit_state)
        || std::ranges::any_of(n.children, [](const WidgetNode& c) { return c.layout_kind == LayoutKind::Handle; }));
}

// The passes after layout skip kept subtrees, so none may hold what they look for: a drop
// target, a popup, a scroll view to clamp (a text area's) or a scrollbar fading out.
inline bool holds_what_passes_look_for(WidgetNode& n) {
    return n.drag.accepts || n.popup_area || n.popup_builder || get_scroll_view(n);
}

// The last publish's layout children of one node, walked in order to find each widget's own.
struct LayoutCacheCursor {
    LayoutNode* owner = nullptr;
    std::size_t next = 0;
    std::size_t* kept = nullptr;   // counts the nodes that can be kept, for the snapshot's stats
    // A few nodes ahead at most: rows inserted or removed shift the rest by little, and a miss
    // only costs a rebuild.
    LayoutNode* take(const WidgetNode& n) {
        if (!owner) return nullptr;
        std::size_t end = std::min(next + 4, owner->children.size());
        for (std::size_t k = next; k < end; ++k)
            if (owner->children[k].source == &n) {
                next = k + 1;
                return &owner->children[k];
            }
        return nullptr;
    }
    bool consumed() const { return !owner || next == owner->children.size(); }
};

// Links `node` and its subtree to their layout nodes from the last publish and marks which can
// be kept as they are. Returns whether all of it can.
inline bool link_layout_cache(WidgetNode& node, LayoutCacheCursor& siblings) {
    bool kept_last_time = std::exchange(node.layout_reusable, false);
    if (passes_through(node)) {
        node.cached_layout = nullptr;
        bool all = true;
        for (auto& c : node.children) all &= link_layout_cache(c, siblings);
        return all;
    }
    LayoutNode* cached = siblings.take(node);
    node.cached_layout = cached;
    // Kept whole last time, and nothing below it changed since (its mark): kept whole again,
    // without walking it. Its own conditions below held then and only change with a redraw.
    if (kept_last_time && cached && cached->id == node.id && !node.dirty && !node.relayout
        && !node.mark->below) {
        node.layout_reusable = true;
        if (siblings.kept) *siblings.kept += node.kept_nodes;
        return true;
    }
    std::size_t kept_before = siblings.kept ? *siblings.kept : 0;
    bool children_kept = true;
    bool split = splits(node);
    if (node.is_container) {
        LayoutCacheCursor inner{cached, 0, siblings.kept};
        for (auto& c : node.children) children_kept &= link_layout_cache(c, inner);
        children_kept &= inner.consumed();
        if (split)
            for (auto& c : node.children) c.layout_reusable = false;
    }
    node.layout_reusable = cached && children_kept && !split && cached->id == node.id && !node.dirty
        && !node.relayout && node.overlay_draws.empty() && cached->overlay_draws.empty()
        && cached->measured_axis.has_value() && !keeps_state(node) && !holds_what_passes_look_for(node);
    if (node.layout_reusable && siblings.kept) ++*siblings.kept;
    node.kept_nodes = siblings.kept ? *siblings.kept - kept_before : 0;
    return node.layout_reusable;
}

// Appends the layout nodes of node's children to `into`, allocating once: a LayoutNode is
// large, and growing the vector one child at a time moved every earlier sibling each time.
inline void build_children(WidgetNode& node, LayoutNode& into, bool keep) {
    into.children.reserve(into.children.size() + node.children.size());
    for (auto& c : node.children)
        build_layout(c, into, keep);
}

inline void build_layout(WidgetNode& node, LayoutNode& parent, bool keep) {
    using LK = LayoutKind;

    if (keep && node.layout_reusable && node.cached_layout) {
        parent.children.push_back(std::move(*node.cached_layout));
        parent.children.back().kept = true;
        node.cached_layout = nullptr;
        return;
    }
    node.relayout = false;
    if (!node.is_container) {
        if (node.layout_kind == LK::Spacer) {
            LayoutNode spacer;
            spacer.kind = LayoutNode::Kind::Spacer;
            spacer.spacer_size = node.spacer_size;
            spacer.id = node.id;
            spacer.source = &node;
            spacer.theme = node.theme;
            parent.children.push_back(std::move(spacer));
        } else if (node.layout_kind == LK::Canvas) {
            LayoutNode canvas;
            canvas.kind = LayoutNode::Kind::Canvas;
            canvas.id = node.id;
            canvas.source = &node;
            canvas.theme = node.theme;
            canvas.overlay_draws = node.overlay_draws;
            canvas.canvas_min_width = node.canvas_min_width;
            canvas.canvas_min_height = node.canvas_min_height;
            canvas.widget = &node;
            parent.children.push_back(std::move(canvas));
        } else if (node.layout_kind == LK::Handle) {
            LayoutNode handle;
            handle.kind = LayoutNode::Kind::Handle;
            handle.id = node.id;
            handle.source = &node;
            handle.theme = node.theme;
            handle.overlay_draws = node.overlay_draws;
            handle.widget = &node;
            parent.children.push_back(std::move(handle));
        } else {
            LayoutNode leaf;
            leaf.kind = LayoutNode::Kind::Leaf;
            leaf.id = node.id;
            leaf.source = &node;
            leaf.theme = node.theme;
            leaf.overlay_draws = node.overlay_draws;
            leaf.hint.expand = node.expand;
            leaf.hint.expand_axis = node.expand_axis;
            if (node.intrinsic_width) {
                if (!node.intrinsic_width_cache) node.intrinsic_width_cache = node.intrinsic_width(node);
                leaf.intrinsic_width = *node.intrinsic_width_cache;
            }
            leaf.widget = &node;
            parent.children.push_back(std::move(leaf));
        }
    } else if (node.layout_kind == LK::Scroll) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Scroll;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        if (auto* ss = std::any_cast<ScrollState>(&node.edit_state))
            container.scroll_offset = ss->offset_y;
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::VirtualList) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::VirtualList;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        if (auto* vls = get_vlist_state(node)) {
            container.scroll_offset = vls->scroll_offset;
            container.scroll_content_h = Height{
                static_cast<float>(vls->item_count.raw()) * vls->item_height.raw()};
            container.vlist_visible_start = vls->visible_start.raw();
            container.vlist_item_height = vls->item_height;
        }
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Table) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Table;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        if (auto* ts = get_table_state(node)) {
            container.scroll_offset = ts->scroll_y;
            container.scroll_content_h = Height{
                static_cast<float>(ts->row_count()) * ts->row_height.raw()};
            container.vlist_visible_start = ts->visible_start.raw();
            container.vlist_item_height = ts->row_height;
            container.table_column_count = ts->columns();
            container.table_header_h = ts->row_height;
            container.table_scroll_x = ts->scroll_x;
        }
        container.overlay_draws = node.overlay_draws;
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Layers) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Layers;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        if (node.layer_places) container.layer_rects = node.layer_places();
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Tabs) {
        LayoutNode container;
        container.kind = LayoutNode::Kind::Tabs;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else if (node.layout_kind == LK::Row || node.layout_kind == LK::Column) {
        LayoutNode container;
        container.kind = (node.layout_kind == LK::Row)
            ? LayoutNode::Kind::Row : LayoutNode::Kind::Column;
        container.id = node.id;
        container.source = &node;
        container.theme = node.theme;
        if (auto* ss = std::any_cast<SplitState>(&node.edit_state); ss && ss->engaged)
            container.split_sizes = ss->pane_sizes;
        container.frame = node.frame;
        build_children(node, container, keep);
        parent.children.push_back(std::move(container));
    } else {
        for (auto& c : node.children)
            build_layout(c, parent, keep);
    }
}

} // namespace prism::app::widget_detail
