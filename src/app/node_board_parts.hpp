#pragma once

// What node_board.cpp, node_board_paint.cpp and node_board_drag.cpp share. Private to src/app.

#include <prism/app/node_board.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>

#include <functional>
#include <optional>

namespace prism::app::node_board_detail {

constexpr float dot_side = 12.f;
constexpr float dot_ring = 3.f;     // of the board's own colour, around a dot
// How far a dot reaches into its card, from the edge: a port's label starts clear of it.
constexpr float dot_clearance = dot_side / 2.f + dot_ring + 2.f;

// Every conversion between the graph's positions, the board's coordinates and the window's (a
// widget's own goes through ui::place_of): a zoom changes this one place.
namespace board_coords {
core::Point on_board(const nodes::Graph& graph, core::Point at);
core::Point dragged(const nodes::Graph& graph, const ui::WidgetNode& n, core::Point from, core::Offset d);
core::Point to_graph(const nodes::Graph& graph, core::Point on_board);
std::optional<core::Point> board_to_window(const NodeBoardParts& parts, core::Point on_board);
std::optional<core::Offset> from_board_origin(const NodeBoardParts& parts, const ui::WidgetNode& n, core::Point local);
ui::BoardRectOf window_rects(const NodeBoardParts& parts);
std::optional<core::Point> window_to_graph(const NodeBoardParts& parts, core::Point window);
} // namespace board_coords

// Where each card with a size lies, in graph units: those the graph still has, in id order.
std::map<nodes::NodeId, core::Rect> card_rects(const NodeBoardParts& parts);

// A fit or a tidy gives up waiting for the sizes to settle after this many passes: a layout that never
// settles must not keep asking for publishes.
constexpr int fit_waits_limit = 3;

// A tidy asked for starts its glide once the card sizes settle; returns whether it started.
bool tidy_as_asked(nodes::Graph& graph, NodeBoardParts& parts, std::size_t cards, bool stable);
// One frame of a tidy's glide, if one is under way.
void glide_on(nodes::Graph& graph, NodeBoardParts& parts);

// Where dragging node `dragged` to `to` puts it once it snaps to its neighbours and levels its wires.
nodes::Snapped snapped_drag(const NodeBoardParts& parts, nodes::NodeId dragged, core::Point to);

// The probe, while the pointer is still on the board's background and its wire is still there.
const Probe* shown_probe(const NodeBoardParts& parts);

// The port whose dot is within reach of `p` (window coordinates) and that `wanted` takes.
std::optional<nodes::PortRef> dot_near(const NodeBoardParts& parts, core::Point p,
                                       const std::function<bool(nodes::PortRef)>& wanted);
bool any_port(nodes::PortRef);

void draw_grid(render::DrawList& dl, core::Size extent, const ui::Theme& t, const nodes::BoardView& view);
void draw_wires(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
                const ui::Theme& t);
void draw_dots(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of, float scale,
               const ui::Theme& t);
void outline_selected_node(render::DrawList& dl, const NodeBoardParts& parts, const ui::BoardRectOf& rect_of,
                           float scale, const ui::Theme& t);
void draw_guides(render::DrawList& dl, const NodeBoardParts& parts, const ui::Theme& t);
void draw_probe(render::DrawList& dl, const NodeBoardParts& parts, const ui::Theme& t);
void draw_trail(render::DrawList& dl, const NodeBoardParts& parts, nodes::PortRef from, core::Point pointer,
                const ui::Theme& t);

ui::DragRole port_source(NodeBoardParts& parts, nodes::PortRef at);
ui::DragRole port_target(nodes::Graph& graph, nodes::PortRef at);
ui::DragRole dot_drag(NodeBoardParts& parts);
ui::DragRole dot_target(NodeBoardParts& parts);

} // namespace prism::app::node_board_detail
