#pragma once

// What vb.node_board keeps between its builds (view_builder.hpp declares the builder calls).

#include <prism/core/field.hpp>
#include <prism/nodes/graph.hpp>
#include <prism/ui/node.hpp>

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism::app {

// Where a port's wires end: its card's edge (left for an input, right for an output), level
// with its row.
struct PortPlace {
    ui::WidgetId card = 0;
    ui::WidgetId row = 0;
    nodes::Dir dir = nodes::Dir::In;
};

struct BoardBackground {
    void canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n);
};

// Rebuilt with the cards; the labels are the texts the cards show, so they live as long.
struct NodeBoardParts {
    std::vector<nodes::NodeId> built;   // the nodes the cards were built for, in board order
    std::map<const nodes::Port*, PortPlace> ports;
    std::vector<std::pair<const nodes::Port*, PortPlace>> card_ports;   // the card being built
    std::deque<core::Field<ui::Label<>>> labels;   // a deque: the widgets point at them
    BoardBackground background;

    core::Field<ui::Label<>>& label(std::string text, bool heading = false) {
        return labels.emplace_back(ui::Label<>{.value = std::move(text), .heading = heading, .wrap = false});
    }
};

// The end of `port`'s wires in board coordinates; none if no row of it was placed.
std::optional<core::Point> wire_end(const NodeBoardParts& parts, const nodes::Port* port,
                                    const ui::BoardRectOf& rect_of);

} // namespace prism::app
