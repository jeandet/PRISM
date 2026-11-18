#pragma once

#include <prism/app/layout_writeback.hpp>
#include <prism/app/widget_tree_layout.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/popup_place.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <memory>
#include <optional>

// The content of a popup that holds widgets (doc/design/popup-content.md): laid out on a card
// of its own and drawn into the window's overlay. WidgetTree::build_snapshot calls these.
namespace prism::app::widget_detail {
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;

struct PopupLayout {
    WidgetNode* anchor;
    LayoutNode content;
    Rect card;   // window coordinates
    bool viewport_moved = false;   // see update_scroll_state_of
};

inline constexpr float popup_pad = 16.f;      // between the card's edge and its content
inline constexpr float popup_margin = 24.f;   // kept free between the card and the window edge
inline constexpr float popup_min_w = 320.f;

// Never in a subtree whose layout was kept (link_layout_cache).
inline WidgetNode* find_open_popup(WidgetNode& node) {
    if (node.layout_reusable) return nullptr;
    if (node.popup_builder && !node.children.empty()) return &node;
    for (auto& c : node.children)
        if (auto* p = find_open_popup(c)) return p;
    return nullptr;
}

// Where the widget is on screen: its place in the layout, moved up by every scrolled
// container above it, as layout_flatten draws it.
// simplify: not zoomed: a popup holding widgets (ui::Popup) whose anchor is in a zoomed board's
// card opens where the anchor would be at zoom 1. Map through the board's zoom (as
// layout_writeback's child_view does) once such a popup is wanted in a node.
inline std::optional<Point> anchor_origin(const LayoutNode& node, WidgetId id) {
    if (node.id == id) return node.allocated.origin;
    for (auto& c : node.children)
        if (auto p = anchor_origin(c, id)) return Point{p->x, p->y - node.scroll_offset};
    return std::nullopt;
}

// The open popup's content, measured, given a card (a modal one centred, a non-modal one
// next to its anchor, both within the window) and arranged in it. Also sets the anchor's
// popup_area, which routing reads.
inline std::optional<PopupLayout> layout_popup(WidgetNode& root, const LayoutNode& main, Size window, const Theme& theme) {
    WidgetNode* anchor = find_open_popup(root);
    if (!anchor) return std::nullopt;
    PopupLayout p{anchor, {}, {}};
    auto& content_wn = anchor->children.front();
    p.content.kind = LayoutNode::Kind::Column;
    p.content.id = content_wn.id;
    p.content.source = &content_wn;
    p.content.theme = &theme;
    for (auto& c : content_wn.children) build_layout(c, p.content);
    layout_measure(p.content, LayoutAxis::Vertical);
    float max_w = std::max(0.f, window.w.raw() - 2 * popup_margin);
    float max_h = std::max(0.f, window.h.raw() - 2 * popup_margin);
    Size size{Width{std::min(max_w, std::max(popup_min_w, p.content.hint.cross + 2 * popup_pad))},
              Height{std::min(max_h, p.content.hint.preferred + 2 * popup_pad)}};
    Point at = anchor_origin(main, anchor->id).value_or(Point{X{0}, Y{0}});
    p.card = anchor->popup_modal
        ? Rect{Point{X{(window.w.raw() - size.w.raw()) / 2}, Y{(window.h.raw() - size.h.raw()) / 2}}, size}
        : place_popup(size, Rect{at, Size{Width{0}, Height{0}}}, window);
    layout_arrange(p.content, Rect{Point{p.card.origin.x + DX{popup_pad}, p.card.origin.y + DY{popup_pad}},
                                   Size{size.w - Width{2 * popup_pad}, size.h - Height{2 * popup_pad}}});
    p.viewport_moved = update_scroll_state(p.content);
    anchor->popup_area = Rect{Point{X{p.card.origin.x.raw() - at.x.raw()}, Y{p.card.origin.y.raw() - at.y.raw()}}, size};
    return p;
}

// Into the overlay, which draws after and is hit-tested before the window's widgets:
// a scrim behind a modal popup, the card, then the content clipped to the card.
inline void flatten_popup(PopupLayout& p, SceneSnapshot& snap, const Theme& theme) {
    if (p.anchor->popup_modal) snap.overlay.filled_rect(Rect{Point{X{0}, Y{0}}, snap.viewport}, theme.scrim);
    snap.overlay.rounded_rect(p.card, theme.popup_bg, 8.f);
    snap.overlay.rounded_rect(p.card, theme.popup_border, 8.f, 1.f);
    snap.overlay_geometry.push_back({p.anchor->id, p.card});   // the card's own background

    SceneSnapshot content;
    content.viewport = snap.viewport;
    DrawList open_clip, close_clip;
    open_clip.clip_push(p.card.origin, p.card.extent);
    close_clip.clip_pop();
    layout_detail::push_entry(content, 0, Rect{}, std::move(open_clip));
    layout_flatten(p.content, content, p.card);
    layout_detail::push_entry(content, 0, Rect{}, std::move(close_clip));
    resolve_clips(content);   // nested clips (a scroll area) cut to the card

    for (auto idx : content.z_order)
        if (content.draw_lists[idx])
            for (const auto& cmd : content.draw_lists[idx]->commands) snap.overlay.commands.push_back(cmd);
    for (std::size_t i = 0; i < content.geometry.size(); ++i) {
        const Rect& visible = content.visible[i];   // already cut to the card
        if (const WidgetId id = content.geometry[i].first; id != 0 && visible.extent.w.raw() > 0 && visible.extent.h.raw() > 0) {
            snap.overlay_geometry.push_back({id, visible});
            snap.overlay_placed.push_back(content.geometry[i]);
        }
    }
    for (const auto& cmd : content.overlay.commands) snap.overlay.commands.push_back(cmd);
    for (auto& g : content.overlay_geometry) snap.overlay_geometry.push_back(g);
    for (auto& g : content.scrollbar_geometry) snap.scrollbar_geometry.push_back(g);
}

} // namespace prism::app::widget_detail
