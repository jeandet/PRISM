#pragma once

#include <prism/nodes/graph.hpp>

#include <functional>
#include <string>
#include <utility>

namespace prism::nodes {

// A named recipe for one node, what a palette lists. `add` makes one at `at` (graph units).
struct NodeKind {
    std::string title;
    std::string family;
    std::function<NodeId(Graph&, core::Point at)> add;
};

// The kind that makes an N, which the graph owns, removable, titled `title` in `family`.
template <class N>
NodeKind kind_of(std::string title, std::string family) {
    return {title, family, [title, family](Graph& g, core::Point at) {
                return g.emplace<N>(NodeOptions{.title = title, .family = family, .at = at, .removable = true});
            }};
}

} // namespace prism::nodes
