#pragma once

#include <prism/core/types.hpp>
#include <prism/nodes/graph.hpp>

#include <map>

namespace prism::nodes {

// The gaps tidy leaves between two columns and between two cards of a column, in graph units.
struct TidySpacing {
    float column_gap = 80.f;
    float row_gap = 40.f;
};

// Where each node of `graph` goes, as a layered drawing
// (https://en.wikipedia.org/wiki/Layered_graph_drawing): a node's column is its longest path from
// a source, the cards of a column are ordered to cross fewer wires, x starts a column after the
// widest card of the one before it, y stacks the cards of a column, and the whole result is moved
// so its bounding box starts where the cards' did. A node with no wires goes to the first column,
// after the wired ones. `sizes`: each node's card size; a node left out of it is a point.
std::map<NodeId, core::Point> tidy_layout(const Graph& graph,
                                          const std::map<NodeId, core::Size>& sizes,
                                          TidySpacing spacing = {});

} // namespace prism::nodes
