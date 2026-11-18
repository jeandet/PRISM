#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/snap.hpp>

#include <algorithm>

namespace prism::app::node_board_detail {

namespace {

constexpr float snap_reach_px = 6.f;

std::vector<core::Rect> other_cards(const NodeBoardParts& parts, nodes::NodeId dragged) {
    std::vector<core::Rect> out;
    for (const auto& [id, rect] : card_rects(parts))
        if (id != dragged) out.push_back(rect);
    return out;
}

// Where a wire ends on `ref`'s port, in graph units.
std::optional<core::Point> wire_end_in_graph(const NodeBoardParts& parts, nodes::PortRef ref) {
    const auto end = wire_end(parts, parts.graph->port(ref), board_coords::window_rects(parts));
    return end ? board_coords::window_to_graph(parts, *end) : std::nullopt;
}

// A port's wire end below its card's top, in graph units: it stays put while the card is moved.
std::optional<core::DY> centre_from_top(const NodeBoardParts& parts, nodes::PortRef ref) {
    const auto place = parts.ports.find(parts.graph->port(ref));
    const auto card = place == parts.ports.end() ? std::nullopt : parts.tree->laid_out_at(place->second.card);
    const auto end = wire_end_in_graph(parts, ref);
    const auto top = card ? board_coords::window_to_graph(parts, card->origin) : std::nullopt;
    return end && top ? std::optional{(*end - *top).dy} : std::nullopt;
}

std::optional<nodes::LevelWith> level_with(const NodeBoardParts& parts, nodes::PortRef mine, nodes::PortRef theirs) {
    const auto below = centre_from_top(parts, mine);
    const auto end = wire_end_in_graph(parts, theirs);
    if (!below || !end) return std::nullopt;
    return nodes::LevelWith{*below, end->y, end->x};
}

std::vector<nodes::LevelWith> wires_of(const NodeBoardParts& parts, nodes::NodeId dragged) {
    std::vector<nodes::LevelWith> out;
    for (const auto id : parts.graph->edges()) {
        const auto* e = parts.graph->edge(id);
        if ((e->from.node == dragged) == (e->to.node == dragged)) continue;
        const bool from_mine = e->from.node == dragged;
        if (const auto w = level_with(parts, from_mine ? e->from : e->to, from_mine ? e->to : e->from)) out.push_back(*w);
    }
    return out;
}

} // namespace

nodes::Snapped snapped_drag(const NodeBoardParts& parts, nodes::NodeId dragged, core::Point to) {
    const auto card = parts.cards.find(dragged);
    const auto size = card == parts.cards.end() ? std::nullopt : parts.tree->arranged_extent(card->second);
    if (!size) return {to, {}};
    const auto others = other_cards(parts, dragged);
    const auto wires = wires_of(parts, dragged);
    return nodes::snap(to, *size, others, wires, snap_reach_px / parts.graph->view().zoom);
}

} // namespace prism::app::node_board_detail
