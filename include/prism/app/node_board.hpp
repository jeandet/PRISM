#pragma once

// What vb.node_board keeps between its builds (view_builder.hpp declares the builder calls).

#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/nodes/graph.hpp>
#include <prism/nodes/node_kind.hpp>
#include <prism/nodes/snap.hpp>
#include <prism/ui/node.hpp>

#include <chrono>
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
    nodes::PortRef ref{};
};

// What dragging from a port carries: the wire's fixed end (the port itself, or the output of the
// wire picked up from an input). It is dropped on a port it can connect to.
struct PortDrag {
    nodes::Graph* graph = nullptr;
    nodes::PortRef from;
};

// What dragging a palette row carries: the kind to add, to the board of `graph` it is dropped on.
struct PaletteDrag {
    nodes::Graph* graph = nullptr;
    nodes::NodeKind kind;
};

// What the board has selected (a wire clicked, a node's header clicked): Delete removes it.
struct BoardSelection {
    std::optional<nodes::EdgeId> wire;
    std::optional<nodes::NodeId> node;
};

// The wire the pointer rests on, and where: the probe's bubble shows its value there.
struct Probe {
    nodes::EdgeId edge = 0;
    core::Offset at{};   // from the board's top-left
};

struct NodeBoardParts;

// The empty board: dragging it pans every node, clicking it selects the wire there (or nothing),
// and with the focus it takes on a click, Delete removes what is selected.
struct BoardBackground {
    NodeBoardParts* parts = nullptr;
    std::optional<core::Point> press;   // in the background's coordinates
    core::Point pan_at_press{};
    std::uint8_t pan_button = input::buttons::left;   // the button panning, while `press` is set
    void canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n);
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect r);
};

// A node's header: dragging it (the gap between its labels, or a label) moves the node, the
// press point staying under the pointer. The header moves with the pointer, and a move is read
// against the frame the user saw, which may still show the node where it was a few moves ago:
// so each move starts from where that frame showed the node, not from where it is now.
// A press on it selects the node.
struct NodeGrip {
    NodeBoardParts* parts = nullptr;
    nodes::NodeId id = 0;
    std::optional<core::Point> press;            // in the pressed widget's coordinates
    std::map<std::uint64_t, core::Point> shown;   // frame version -> where it first shows the node
    void canvas(render::DrawList&, core::Rect, const ui::WidgetNode&) {}
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& n, core::Rect r);
};

// Nodes gliding to where tidy puts them: each from where it was, over 200 ms.
struct Glide {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point next_step;   // no node moves before this
    std::map<nodes::NodeId, std::pair<core::Point, core::Point>> legs;   // node -> from, to
};

// Kept for the board's life; the cards' parts are rebuilt with the cards, and the labels are the
// texts the cards show, so they live as long.
struct NodeBoardParts {
    nodes::Graph* graph = nullptr;
    WidgetTree* tree = nullptr;
    BoardSelection selected;
    std::optional<Probe> probe;
    core::Connection probe_link;       // wakes the board when the probed output changes
    core::Connection structure_link;   // drops the probe when its wire is gone
    std::vector<nodes::NodeId> built;   // the nodes the cards were built for, in board order
    std::map<const nodes::Port*, PortPlace> ports;
    std::map<nodes::NodeId, ui::WidgetId> cards;
    std::vector<nodes::Guide> guides;   // the snap guides of the drag going on, in graph units
    nodes::NodeId card_node = 0;   // the card being built
    std::vector<std::pair<const nodes::Port*, PortPlace>> card_ports;   // likewise
    std::deque<core::Field<ui::Label<>>> labels;   // a deque: the widgets point at them
    std::deque<NodeGrip> grips;                     // likewise
    std::optional<Glide> glide;   // a tidy under way
    BoardBackground background;
    ui::WidgetId background_id = 0;
    ui::WidgetId board_id = 0;
    // The board and card sizes the last layout pass saw: a fit asked for at startup waits for two
    // passes at the same size, since a wrapped label above the board settles on a later one.
    std::optional<core::Size> seen_board;
    std::vector<core::Size> seen_cards;
    int tidy_waits = 0;   // likewise for a tidy waiting for its cards
    int fit_waits = 0;   // passes this fit has waited for, so a size that never settles fits anyway

    core::Field<ui::Label<>>& label(std::string text, bool heading = false) {
        return labels.emplace_back(ui::Label<>{.value = std::move(text), .heading = heading, .wrap = false,
                                               .flat = true});
    }
};

// The end of `port`'s wires, in the coordinates `rect_of` gives; none if no row of it was placed.
std::optional<core::Point> wire_end(const NodeBoardParts& parts, const nodes::Port* port,
                                    const ui::BoardRectOf& rect_of);

} // namespace prism::app
