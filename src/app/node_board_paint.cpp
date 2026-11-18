#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/wire_geometry.hpp>
#include <prism/ui/text_metrics.hpp>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>

namespace prism::app::node_board_detail {

namespace {

constexpr float grid_step = 20.f;      // board units between two grid lines
constexpr float grid_floor = 8.f;      // on screen, px: closer than this the grid is left out
constexpr float grid_dot_radius = 1.5f;   // covers its pixel even when it sits on a pixel corner at 1x

// Where the grid's first line falls in the board's own coordinates. The background is unzoomed and
// a card at `position` sits at zoom * (position + pan), so the grid follows the same pan and zoom.
float first_line(float moved, float step) {
    const float rest = std::fmod(moved, step);
    return rest < 0.f ? rest + step : rest;
}

// `fn` once per grid line from `first` up to `extent`, `step` apart.
template <class Fn>
void for_each_line(float first, float step, float extent, Fn&& fn) {
    for (int i = 0;; ++i) {
        const float at = first + static_cast<float>(i) * step;
        if (at > extent) return;
        fn(at);
    }
}

} // namespace

void draw_grid(render::DrawList& dl, core::Size extent, const ui::Theme& t, const nodes::BoardView& view) {
    // simplify: one command per dot (or per line), every frame a view or theme change redraws it.
    // A fine grid on a large board is thousands of commands; a tiled pattern the backend repeats
    // is the upgrade, once drawing them costs more than it is worth.
    const float step = grid_step * view.zoom;
    if (step < grid_floor) return;
    const float x0 = first_line(view.zoom * view.pan.x.raw(), step);
    const float y0 = first_line(view.zoom * view.pan.y.raw(), step);
    if (t.node_grid_dots) {
        for_each_line(y0, step, extent.h.raw(), [&](float y) {
            for_each_line(x0, step, extent.w.raw(), [&](float x) {
                dl.circle(core::Point{core::X{x}, core::Y{y}}, grid_dot_radius, t.node_grid);
            });
        });
        return;
    }
    for_each_line(x0, step, extent.w.raw(), [&](float x) {
        dl.line(core::Point{core::X{x}, core::Y{0.f}}, core::Point{core::X{x}, core::Y{extent.h.raw()}},
                t.node_grid, 1.f);
    });
    for_each_line(y0, step, extent.h.raw(), [&](float y) {
        dl.line(core::Point{core::X{0.f}, core::Y{y}}, core::Point{core::X{extent.w.raw()}, core::Y{y}},
                t.node_grid, 1.f);
    });
}

namespace {

constexpr float thin_wire = 2.f;
constexpr float thick_wire = 3.f;   // a waveform's: it carries a curve, not a scalar
constexpr float wire_under = 9.f;   // the soft stroke drawn under a selected wire
constexpr float wire_under_alpha = 0.25f;
constexpr float dot_side_floor = 4.f;
constexpr float selected_ring = 2.f;   // the selected card's border and the glow outside it
constexpr float faded_port_alpha = 0.35f;

// A size drawn to the cards' zoom, never below `floor` px: a wire or a dot zoomed far out stays seen.
float zoomed(float size, float scale, float floor) { return std::max(size * scale, floor); }

// `c` at a fraction of its own alpha: a selected wire's under-stroke, a port a drag cannot take.
Color faded(Color c, float by) {
    c.a = static_cast<std::uint8_t>(float(c.a) * by + 0.5f);
    return c;
}

// A port's colour: the theme's wire colour for its type's slot.
Color port_color(const ui::Theme& t, const nodes::Port& port) {
    return t.node_wires[nodes::wire_slot(port.type())];
}

// A wire's stroke: a waveform is drawn thicker than the scalars are.
float wire_width(const nodes::Port& port) {
    static const nodes::TypeKey waveform = nodes::type_key<std::vector<double>>();
    return port.type() == waveform ? thick_wire : thin_wire;
}

// `side` square, centred on `at`.
core::Rect square_at(core::Point at, float side) {
    return {core::Point{core::X{at.x.raw() - side / 2.f}, core::Y{at.y.raw() - side / 2.f}},
            core::Size{core::Width{side}, core::Height{side}}};
}

// How opaque a port's dot is: while a wire is dragged, the dots it cannot be dropped on fade.
float port_alpha(const NodeBoardParts& parts, nodes::PortRef port) {
    const auto* drag = parts.tree->drag();
    if (!drag || !drag->value.has_value()) return 1.f;
    const auto* wire = std::any_cast<PortDrag>(&drag->value);
    if (!wire || wire->graph != parts.graph) return 1.f;
    return parts.graph->can_connect(wire->from, port) ? 1.f : faded_port_alpha;
}
} // namespace

// The probe, while the pointer is still on the board's background (a card or the window's edge
// took the hover) and its wire is still there.
const Probe* shown_probe(const NodeBoardParts& parts) {
    const bool on_background = parts.tree->hovered_id() == parts.background_id;
    return on_background && parts.probe && parts.graph->edge(parts.probe->edge) ? &*parts.probe : nullptr;
}

void draw_wires(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
                const ui::Theme& t) {
    const Probe* probe = shown_probe(parts);
    const nodes::Graph& graph = *parts.graph;
    for (const auto id : graph.edges()) {
        const auto* e = graph.edge(id);
        const auto* port = graph.port(e->from);
        if (!port) continue;
        const auto from = wire_end(parts, port, rect_of);
        const auto to = wire_end(parts, graph.port(e->to), rect_of);
        if (!from || !to) continue;
        const Color color = port_color(t, *port);
        const auto curve = nodes::polyline(nodes::wire_curve(*from, *to));
        if (parts.selected.wire == id || (probe && probe->edge == id))
            dl.polyline(curve, faded(color, wire_under_alpha), zoomed(wire_under, scale, 1.f));
        dl.polyline(curve, color, zoomed(wire_width(*port), scale, 1.f));
    }
}

namespace {

constexpr float probe_text_size = 11.f;
constexpr float probe_pad_x = 8.f, probe_pad_y = 3.f;
constexpr float probe_radius = 10.f;
constexpr float probe_gap = 12.f;   // from the pointer, to the right and up

// `at` moved to keep a box of `box` inside one of `board`'s size.
core::Point kept_inside(core::Point at, core::Size box, core::Size board) {
    const auto keep = [](float v, float room) { return std::clamp(v, 0.f, std::max(room, 0.f)); };
    return {core::X{keep(at.x.raw(), board.w.raw() - box.w.raw())},
            core::Y{keep(at.y.raw(), board.h.raw() - box.h.raw())}};
}

} // namespace

// The probe's bubble, at natural size whatever the zoom: the value in the wire's colour-ringed box.
void draw_probe(render::DrawList& dl, const NodeBoardParts& parts, const ui::Theme& t) {
    const Probe* probe = shown_probe(parts);
    const auto board = parts.tree->laid_out_at(parts.board_id);
    if (!probe || !board) return;
    const nodes::Port* port = parts.graph->port(parts.graph->edge(probe->edge)->from);
    if (!port) return;
    const std::string text = nodes::format_value(port->type(), port->value());
    const render::FontStyle mono{render::FontRole::Mono};
    const core::Size box{core::Width{ui::text_width(t.measurer, text, probe_text_size, mono) + 2.f * probe_pad_x},
                         core::Height{ui::line_height(t.measurer, probe_text_size, mono) + 2.f * probe_pad_y}};
    const core::Point pointer = core::Point{} + probe->at;
    const core::Point origin = kept_inside(pointer + core::Offset{core::DX{probe_gap}, core::DY{-probe_gap - box.h.raw()}},
                                           box, board->extent);
    const core::Rect rect{origin, box};
    dl.rounded_rect(rect, t.node_probe_bg, probe_radius);
    dl.rounded_rect(rect, port_color(t, *port), probe_radius, 1.f);
    dl.text(text, origin + core::Offset{core::DX{probe_pad_x}, core::DY{probe_pad_y}}, probe_text_size,
            t.node_probe_text, 0.f, render::TextAnchor::TopLeft, mono);
}

namespace {

constexpr float guide_alpha = 0.6f;
} // namespace

void draw_guides(render::DrawList& dl, const NodeBoardParts& parts, const ui::Theme& t) {
    for (const auto& g : parts.guides)
        dl.line(nodes::board_point(parts.graph->view(), g.from), nodes::board_point(parts.graph->view(), g.to), faded(t.accent, guide_alpha), 1.f);
}

namespace {

// A port's dot: a rounded square in its type's colour, ringed in the board's so it reads where it
// sits on a card's edge.
void draw_dot(render::DrawList& dl, core::Point at, float side, float ring, Color color, const ui::Theme& t) {
    const float corner = std::max(side / 4.f, 1.f);
    dl.rounded_rect(square_at(at, side + 2.f * ring), t.node_board_bg, corner + ring);
    dl.rounded_rect(square_at(at, side), color, corner);
}
} // namespace

void draw_dots(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
               const ui::Theme& t) {
    const float side = zoomed(dot_side, scale, dot_side_floor);
    const float ring = zoomed(dot_ring, scale, 1.f);
    for (const auto& [port, place] : parts.ports)
        if (const auto at = wire_end(parts, port, rect_of))
            draw_dot(dl, *at, side, ring, faded(port_color(t, *port), port_alpha(parts, place.ref)), t);
}

namespace {

// `r` grown by `by` on every side: the selected card's ring sits outside its border.
core::Rect grown(core::Rect r, float by) {
    return {core::Point{core::X{r.origin.x.raw() - by}, core::Y{r.origin.y.raw() - by}},
            core::Size{core::Width{r.extent.w.raw() + 2.f * by}, core::Height{r.extent.h.raw() + 2.f * by}}};
}
} // namespace

void outline_selected_node(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of,
                           float scale, const ui::Theme& t) {
    const auto card = parts.selected.node ? parts.cards.find(*parts.selected.node) : parts.cards.end();
    if (card == parts.cards.end()) return;
    const auto r = rect_of(card->second);
    if (!r) return;
    const float ring = zoomed(selected_ring, scale, 1.f);
    const float radius = ui::node_card_radius * scale;
    dl.rounded_rect(grown(*r, ring), t.node_glow, radius + ring, ring);
    dl.rounded_rect(*r, t.node_border_selected, radius, ring);
}

// A wire being drawn: from the fixed end's port to the pointer (window coordinates), in the
// dragged port's own colour.
void draw_trail(render::DrawList& dl, const NodeBoardParts& parts, nodes::PortRef from, core::Point pointer,
                const ui::Theme& t) {
    const nodes::Port* port = parts.graph->port(from);
    if (!port) return;
    const auto end = wire_end(parts, port, board_coords::window_rects(parts));
    if (!end) return;
    const auto curve = port->dir() == nodes::Dir::Out ? nodes::wire_curve(*end, pointer) : nodes::wire_curve(pointer, *end);
    dl.polyline(nodes::polyline(curve), port_color(t, *port),
                zoomed(wire_width(*port), parts.graph->view().zoom, 1.f));
}

} // namespace prism::app::node_board_detail
