#include "node_board_parts.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/snap.hpp>

#include <algorithm>

namespace prism::app::node_board_detail {

namespace {

constexpr float snap_reach_px = 6.f;

std::vector<core::Rect> other_cards(const NodeBoardParts& parts, nodes::NodeId dragged) {
    std::vector<core::Rect> out;
    for (const auto& [id, card] : parts.cards)
        if (id != dragged && std::ranges::contains(parts.graph->nodes(), id))
            if (const auto size = parts.tree->arranged_extent(card)) out.push_back({parts.graph->position(id), *size});
    return out;
}

// A port's row centre below its card's top, in graph units (rows are laid out at the board's zoom).
std::optional<core::DY> centre_from_top(const NodeBoardParts& parts, nodes::PortRef ref) {
    const auto place = parts.ports.find(parts.graph->port(ref));
    if (place == parts.ports.end()) return std::nullopt;
    const auto row = parts.tree->laid_out_at(place->second.row);
    const auto card = parts.tree->laid_out_at(place->second.card);
    if (!row || !card) return std::nullopt;
    const float below = row->origin.y.raw() + row->extent.h.raw() / 2.f - card->origin.y.raw();
    return core::DY{below / parts.graph->view().zoom};
}

// Where a wire ends on `ref`'s card, in graph units: its outputs on the right edge, its inputs on the left.
std::optional<core::Point> wire_end_in_graph(const NodeBoardParts& parts, nodes::PortRef ref) {
    const auto card = parts.cards.find(ref.node);
    const auto below = centre_from_top(parts, ref);
    const auto size = card == parts.cards.end() ? std::nullopt : parts.tree->arranged_extent(card->second);
    if (!below || !size) return std::nullopt;
    const core::Point at = parts.graph->position(ref.node);
    const float right = parts.graph->port(ref)->dir() == nodes::Dir::Out ? size->w.raw() : 0.f;
    return core::Point{core::X{at.x.raw() + right}, at.y + *below};
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
