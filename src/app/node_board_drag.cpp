#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>

#include <algorithm>
#include <any>
#include <iterator>

namespace prism::app::node_board_detail {

// The port whose dot is within port_reach of `p` (window coordinates) and that `wanted` takes,
// the nearest if several are. Generous: a dot is small, and half of it lies off its card.
std::optional<nodes::PortRef> dot_near(const NodeBoardParts& parts, core::Point p,
                                       const std::function<bool(nodes::PortRef)>& wanted) {
    constexpr float port_reach = 10.f;
    const auto rect_of = board_coords::window_rects(parts);
    std::optional<nodes::PortRef> best;
    float nearest = port_reach;
    for (const auto& [port, place] : parts.ports) {
        const auto at = wire_end(parts, port, rect_of);
        if (!at || !wanted(place.ref)) continue;
        if (const float d = (*at - p).length(); d <= nearest) {
            nearest = d;
            best = place.ref;
        }
    }
    return best;
}

bool any_port(nodes::PortRef) { return true; }

namespace {

// What a drag from port `at` carries (a PortDrag). From an input with a wire, it picks the wire
// up: the wire goes now, and comes back only where the drag is dropped on a port taking it. A
// drop refused, or on nothing, leaves it gone, as node editors do. Nothing if the port's node
// is gone already (the cards are rebuilt on the next frame).
std::any pick_up(nodes::Graph& graph, nodes::PortRef at) {
    const nodes::Port* port = graph.port(at);
    if (!port) return {};
    if (port->dir() == nodes::Dir::In)
        if (const auto e = graph.edge_into(at)) {
            const nodes::PortRef from = graph.edge(*e)->from;
            graph.disconnect(*e);
            return PortDrag{&graph, from};
        }
    return PortDrag{&graph, at};
}

const PortDrag* port_drag_of(const nodes::Graph* graph, const std::any& v) {
    const auto* d = std::any_cast<PortDrag>(&v);
    return d && d->graph == graph ? d : nullptr;
}

// What every drag of a wire shares: the wire follows the pointer, and the ports taking it are
// hinted.
ui::DragRole wire_drag(NodeBoardParts& parts) {
    ui::DragRole role;
    role.trail = [&parts, &theme = parts.tree->theme()](render::DrawList& dl, const std::any& v, core::Point pointer) {
        draw_trail(dl, parts, std::any_cast<const PortDrag&>(v).from, pointer, theme);
    };
    role.hint_targets = true;
    return role;
}

} // namespace

// A port's name: dragging it draws a wire from the port.
ui::DragRole port_source(NodeBoardParts& parts, nodes::PortRef at) {
    ui::DragRole role = wire_drag(parts);
    role.source = [graph = parts.graph, at](const ui::WidgetNode&, core::Point) { return pick_up(*graph, at); };
    return role;
}

// A port's row: a drop of a wire it can take, anywhere on it, connects it.
ui::DragRole port_target(nodes::Graph& graph, nodes::PortRef at) {
    ui::DragRole role;
    role.accepts = [&graph, at](const std::any& v) {
        const auto* d = port_drag_of(&graph, v);
        return d && graph.can_connect(d->from, at);
    };
    role.drop = [&graph, at](const std::any& v, ui::DropPlace) {
        static_cast<void>(graph.connect(std::any_cast<const PortDrag&>(v).from, at));
    };
    return role;
}

namespace {

// The port whose row holds `p` (window coordinates), or else whose dot is near it.
std::optional<nodes::PortRef> port_under(const NodeBoardParts& parts, core::Point p) {
    for (const auto& [port, place] : parts.ports)
        if (const auto row = parts.tree->laid_out_at(place.row); row && row->contains(p)) return place.ref;
    return dot_near(parts, p, any_port);
}

} // namespace

// The background and the cards: a press on a port's row (beside its name) or near its dot
// draws a wire from it. Half of a dot lies off its card, on the background.
ui::DragRole dot_drag(NodeBoardParts& parts) {
    ui::DragRole role = wire_drag(parts);
    role.source = [&parts](const ui::WidgetNode& self, core::Point at) {
        const auto port = port_under(parts, ui::place_of(self).to_window(at));
        return port ? pick_up(*parts.graph, *port) : std::any{};
    };
    return role;
}

namespace {

// The port whose dot near `p` (window coordinates) takes the drag `v`.
std::optional<nodes::PortRef> dot_taking(const NodeBoardParts& parts, const std::any& v, core::Point p) {
    const auto* d = port_drag_of(parts.graph, v);
    if (!d) return std::nullopt;
    return dot_near(parts, p, [&](nodes::PortRef to) { return parts.graph->can_connect(d->from, to); });
}

const PaletteDrag* palette_drag_of(const NodeBoardParts& parts, const std::any& v) {
    const auto* d = std::any_cast<PaletteDrag>(&v);
    return d && d->graph == parts.graph ? d : nullptr;
}

bool carrying_a_node(const NodeBoardParts& parts) {
    const auto* drag = parts.tree->drag();
    return drag && drag->value.has_value() && palette_drag_of(parts, drag->value);
}

} // namespace

// The board: a drop near a dot connects to its port; a palette row dropped anywhere adds its node. It takes drops only there, so it is not
// hinted whole; the port taking one is marked by a bar on its card's edge (the drop's slot,
// an index into parts.ports).
// simplify: the slot, and so the bar, is the nearest dot, as DragRole::slot is not given the
// dragged value; the drop goes to the nearest dot taking it. They differ only when an
// incompatible dot is nearer, both within reach. Pass the value to slot() if that shows.
ui::DragRole dot_target(NodeBoardParts& parts) {
    constexpr float bar = 3.f;
    ui::DragRole role;
    role.accepts = [&parts](const std::any& v) {
        return port_drag_of(parts.graph, v) != nullptr || palette_drag_of(parts, v) != nullptr;
    };
    role.takes_at = [&parts](const std::any& v, ui::DropPlace place) {
        if (palette_drag_of(parts, v)) return true;
        const auto p = board_coords::board_to_window(parts, place.at);
        return p && dot_taking(parts, v, *p);
    };
    role.slot = [&parts](const ui::WidgetNode& self, core::Point at) {
        if (carrying_a_node(parts)) return parts.ports.size();   // no dot: no bar
        const auto port = dot_near(parts, ui::place_of(self).to_window(at), any_port);
        const auto it = std::ranges::find_if(parts.ports, [&](const auto& kv) { return port && kv.second.ref == *port; });
        return static_cast<std::size_t>(std::distance(parts.ports.begin(), it));
    };
    role.slot_marker = [&parts](const ui::WidgetNode& self, std::size_t slot) {
        if (slot >= parts.ports.size()) return core::Rect{};
        const auto& [port, place] = *std::next(parts.ports.begin(), static_cast<std::ptrdiff_t>(slot));
        const auto row = parts.tree->laid_out_at(place.row);
        const auto end = wire_end(parts, port, board_coords::window_rects(parts));
        if (!row || !end) return core::Rect{};
        return core::Rect{ui::place_of(self).to_local(core::Point{core::X{end->x.raw() - bar / 2}, row->origin.y}),
                          core::Size{core::Width{bar}, row->extent.h}};
    };
    role.drop = [&parts](const std::any& v, ui::DropPlace place) {
        if (const auto* node = palette_drag_of(parts, v))
            return static_cast<void>(node->kind.add(*parts.graph, board_coords::to_graph(*parts.graph, place.at)));
        const auto p = board_coords::board_to_window(parts, place.at);
        if (const auto to = p ? dot_taking(parts, v, *p) : std::nullopt)
            static_cast<void>(parts.graph->connect(std::any_cast<const PortDrag&>(v).from, *to));
    };
    return role;
}

} // namespace prism::app::node_board_detail
