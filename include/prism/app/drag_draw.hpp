#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/dock.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/node.hpp>

#include <optional>

// What a drag inside the app draws on top of the window (doc/design/drag-and-docking.md): the
// marker on the target under the pointer, and the dragged widget following it. Pure geometry;
// WidgetTree::draw_drag calls these with its drag state.
namespace prism::app::drag_draw {
using namespace prism::render;
using namespace prism::ui;

// The part of a draw command inside `area`: a rectangle cut to it, text whose origin is in it;
// nothing for the rest (lines, clips, what lies outside).
inline std::optional<DrawCmd> cut_to(const DrawCmd& cmd, Rect area) {
    return std::visit([&](const auto& c) -> std::optional<DrawCmd> {
        if constexpr (requires { c.rect; }) {
            float x0 = std::max(c.rect.origin.x.raw(), area.origin.x.raw());
            float x1 = std::min(c.rect.origin.x.raw() + c.rect.extent.w.raw(), area.origin.x.raw() + area.extent.w.raw());
            if (x1 <= x0) return std::nullopt;
            auto cut = c;
            cut.rect = Rect{Point{X{x0}, c.rect.origin.y}, Size{Width{x1 - x0}, c.rect.extent.h}};
            return cut;
        } else if constexpr (requires { c.origin; }) {
            return area.contains(c.origin) ? std::optional<DrawCmd>{c} : std::nullopt;
        } else {
            return std::nullopt;
        }
    }, cmd);
}

// The dragged widget as it follows the pointer: its draws (only those in `area`, the part
// dragged, if there is one), moved so they start at `at`, and faded.
inline DrawList ghost(const DrawList& source, std::optional<Rect> area, Point at) {
    DrawList out;
    for (const auto& c : source.commands)
        if (!area) out.commands.push_back(c);
        else if (auto kept = cut_to(c, *area)) out.commands.push_back(*kept);
    layout_detail::translate_draw_list(out, DX{at.x.raw()}, DY{at.y.raw()});
    fade(out, 0.6f);
    return out;
}

// The part of `r` a panel dropped on `zone` would take (half of it for an edge).
inline Rect zone_area(Rect r, DockZone zone) {
    float w = r.extent.w.raw(), h = r.extent.h.raw();
    switch (zone) {
    case DockZone::Middle: return r;
    case DockZone::Left: return {r.origin, Size{Width{w / 2}, r.extent.h}};
    case DockZone::Right: return {Point{r.origin.x + DX{w / 2}, r.origin.y}, Size{Width{w / 2}, r.extent.h}};
    case DockZone::Top: return {r.origin, Size{r.extent.w, Height{h / 2}}};
    case DockZone::Bottom: return {Point{r.origin.x, r.origin.y + DY{h / 2}}, Size{r.extent.w, Height{h / 2}}};
    }
    return r;
}

// One button of a guide: a rounded square, a small window inside, the side it docks to filled;
// lit (in the accent) when the pointer is on it. A light surface either way, so it reads on a
// light theme and a dark one.
inline void guide_button(DrawList& dl, Rect r, DockZone zone, bool lit, Color accent) {
    auto surface = Color::rgba(250, 250, 252, 235);
    auto ink = lit ? Color::rgba(255, 255, 255) : accent;
    dl.rounded_rect(r, lit ? accent : surface, 4.f);
    dl.rounded_rect(r, accent, 4.f, 1.f);
    float inset = r.extent.w.raw() / 5.f;
    Rect window{Point{r.origin.x + DX{inset}, r.origin.y + DY{inset}},
                Size{Width{r.extent.w.raw() - 2 * inset}, Height{r.extent.h.raw() - 2 * inset}}};
    dl.rect_outline(window, ink, 1.25f);
    if (zone != DockZone::Middle) dl.filled_rect(zone_area(window, zone), ink);
}

// The cross's bounds grown by a margin: a light plate behind the buttons, so they read over
// whatever the group shows.
inline void guide_plate(DrawList& dl, Rect r, Size group, Color accent) {
    auto buttons = ui::dock_guide(group);
    Rect left = buttons[1].second, right = buttons[2].second, top = buttons[3].second, bottom = buttons[4].second;
    float pad = left.extent.w.raw() / 4.f;
    float x0 = left.origin.x.raw() - pad, x1 = right.origin.x.raw() + right.extent.w.raw() + pad;
    float y0 = top.origin.y.raw() - pad, y1 = bottom.origin.y.raw() + bottom.extent.h.raw() + pad;
    Rect plate{Point{r.origin.x + DX{x0}, r.origin.y + DY{y0}}, Size{Width{x1 - x0}, Height{y1 - y0}}};
    auto edge = accent;
    edge.a = 90;
    dl.rounded_rect(plate, Color::rgba(250, 250, 252, 200), 8.f);
    dl.rounded_rect(plate, edge, 8.f, 1.f);
}

// A group's guide: a plate, and the five buttons on it (ui::dock_guide), the one the pointer
// is on lit.
inline void guide(DrawList& dl, Rect r, DropPlace at, Color accent) {
    DockZone over = ui::dock_zone(at);
    guide_plate(dl, r, at.size, accent);
    for (auto& [zone, local] : ui::dock_guide(at.size)) {
        Rect button{Point{r.origin.x + DX{local.origin.x.raw()}, r.origin.y + DY{local.origin.y.raw()}}, local.extent};
        guide_button(dl, button, zone, zone == over, accent);
    }
}

// A dock's edge guide: the four buttons (ui::dock_edge_guide), the one under the pointer lit.
inline void edge_guide(DrawList& dl, Rect r, Point pointer, Color accent) {
    Point at{X{pointer.x.raw() - r.origin.x.raw()}, Y{pointer.y.raw() - r.origin.y.raw()}};
    auto over = ui::dock_edge_zone(DropPlace{at, r.extent});
    for (auto& [zone, local] : ui::dock_edge_guide(r.extent)) {
        Rect button{Point{r.origin.x + DX{local.origin.x.raw()}, r.origin.y + DY{local.origin.y.raw()}}, local.extent};
        guide_button(dl, button, zone, over == zone, accent);
    }
}

// The area a drop would take: shaded in the accent, outlined.
inline void preview(DrawList& dl, Rect area, Color accent) {
    auto shade = accent;
    shade.a = 60;
    dl.filled_rect(area, shade);
    dl.rect_outline(area, accent, 2.f);
}

inline DropPlace place_in(Rect r, Point pointer) {
    return {Point{X{pointer.x.raw() - r.origin.x.raw()}, Y{pointer.y.raw() - r.origin.y.raw()}}, r.extent};
}

// A dock group under a dragged panel: the area the panel would take (ui/dock.hpp).
inline void dock_zone(DrawList& dl, Rect r, Point pointer, Color accent) {
    preview(dl, zone_area(r, ui::dock_zone(place_in(r, pointer))), accent);
}

// A dock under a dragged panel: the whole side an edge button would give it, or an outline while
// the pointer is between the buttons (nothing there is a drop of its own).
inline void dock_edge_marker(DrawList& dl, Rect r, Point pointer, Color accent) {
    Point at{X{pointer.x.raw() - r.origin.x.raw()}, Y{pointer.y.raw() - r.origin.y.raw()}};
    auto side = ui::dock_edge_zone(DropPlace{at, r.extent});
    if (!side) return dl.rect_outline(r, accent, 2.f);
    preview(dl, zone_area(r, *side), accent);
}

// The guide of a dock group under the pointer, drawn last, over the dragged panel, as docking
// libraries do: the button under the pointer stays in sight.
inline void drop_guide(DrawList& dl, Rect r, Point pointer, const DragRole& role, Color accent) {
    if (role.zones) guide(dl, r, place_in(r, pointer), accent);
}

// Where a drop on target `r` would land: a dock group's zone, a line on the nearer edge of a
// list or tree row (an outline on a tree folder's middle), or an outline around anything else.
inline void drop_marker(DrawList& dl, Rect r, Point pointer, const DragRole& role, Color accent) {
    if (role.zones) return dock_zone(dl, r, pointer, accent);
    if (role.edge_zones) return dock_edge_marker(dl, r, pointer, accent);
    float y_in = pointer.y.raw() - r.origin.y.raw(), h = r.extent.h.raw();
    bool inside = role.inside_middle && y_in > h * 0.25f && y_in < h * 0.75f;
    if (!role.between || inside) return dl.rect_outline(r, accent, 2.f);
    Y y = y_in > h / 2 ? r.origin.y + DY{h} : r.origin.y;
    dl.line(Point{r.origin.x, y}, Point{r.origin.x + DX{r.extent.w.raw()}, y}, accent, 2.f);
}

} // namespace prism::app::drag_draw
