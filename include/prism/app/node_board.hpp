#pragma once

// What vb.node_board keeps between its builds (view_builder.hpp declares the builder calls).

#include <prism/core/field.hpp>
#include <prism/nodes/graph.hpp>
#include <prism/ui/node.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism::app {

class WidgetTree;

// Where a port's wires end: its card's edge (left for an input, right for an output), level
// with its row.
struct PortPlace {
    ui::WidgetId card = 0;
    ui::WidgetId row = 0;
    nodes::Dir dir = nodes::Dir::In;
};

// The empty board: dragging it pans every node.
struct BoardBackground {
    nodes::Graph* graph = nullptr;
    std::optional<core::Point> press;   // in the background's coordinates
    core::Point pan_at_press{};
    void canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n);
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect r);
};

// A node's header: dragging it (the gap between its labels, or a label) moves the node, the
// press point staying under the pointer. The header moves with the pointer, and a move is read
// against the frame the user saw, which may still show the node where it was a few moves ago:
// so each move starts from where that frame showed the node, not from where it is now.
struct NodeGrip {
    nodes::Graph* graph = nullptr;
    nodes::NodeId id = 0;
    const WidgetTree* tree = nullptr;
    std::optional<core::Point> press;            // in the pressed widget's coordinates
    std::map<std::uint64_t, core::Point> shown;   // frame version -> where it first shows the node
    void canvas(render::DrawList&, core::Rect, const ui::WidgetNode&) {}
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect r);
};

// Rebuilt with the cards; the labels are the texts the cards show, so they live as long.
struct NodeBoardParts {
    std::vector<nodes::NodeId> built;   // the nodes the cards were built for, in board order
    std::map<const nodes::Port*, PortPlace> ports;
    std::vector<std::pair<const nodes::Port*, PortPlace>> card_ports;   // the card being built
    std::deque<core::Field<ui::Label<>>> labels;   // a deque: the widgets point at them
    std::deque<NodeGrip> grips;                     // likewise
    BoardBackground background;

    core::Field<ui::Label<>>& label(std::string text, bool heading = false) {
        return labels.emplace_back(ui::Label<>{.value = std::move(text), .heading = heading, .wrap = false});
    }
};

// The end of `port`'s wires in board coordinates; none if no row of it was placed.
std::optional<core::Point> wire_end(const NodeBoardParts& parts, const nodes::Port* port,
                                    const ui::BoardRectOf& rect_of);

} // namespace prism::app
