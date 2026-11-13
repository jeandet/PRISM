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

// A dock group under a dragged panel: the area the panel would take, shaded (ui/dock.hpp).
inline void dock_zone(DrawList& dl, Rect r, Point pointer, Color accent) {
    float w = r.extent.w.raw(), h = r.extent.h.raw();
    Point at{X{pointer.x.raw() - r.origin.x.raw()}, Y{pointer.y.raw() - r.origin.y.raw()}};
    Rect area = r;
    switch (ui::dock_zone(DropPlace{at, r.extent})) {
    case DockZone::Middle: break;
    case DockZone::Left: area.extent.w = Width{w / 2}; break;
    case DockZone::Right: area.origin.x = r.origin.x + DX{w / 2}; area.extent.w = Width{w / 2}; break;
    case DockZone::Top: area.extent.h = Height{h / 2}; break;
    case DockZone::Bottom: area.origin.y = r.origin.y + DY{h / 2}; area.extent.h = Height{h / 2}; break;
    }
    auto shade = accent;
    shade.a = 60;
    dl.filled_rect(area, shade);
    dl.rect_outline(area, accent, 2.f);
}

// Where a drop on target `r` would land: a dock group's zone, a line on the nearer edge of a
// list or tree row (an outline on a tree folder's middle), or an outline around anything else.
inline void drop_marker(DrawList& dl, Rect r, Point pointer, const DragRole& role, Color accent) {
    if (role.zones) return dock_zone(dl, r, pointer, accent);
    float y_in = pointer.y.raw() - r.origin.y.raw(), h = r.extent.h.raw();
    bool inside = role.inside_middle && y_in > h * 0.25f && y_in < h * 0.75f;
    if (!role.between || inside) return dl.rect_outline(r, accent, 2.f);
    Y y = y_in > h / 2 ? r.origin.y + DY{h} : r.origin.y;
    dl.line(Point{r.origin.x, y}, Point{r.origin.x + DX{r.extent.w.raw()}, y}, accent, 2.f);
}

} // namespace prism::app::drag_draw
