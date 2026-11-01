#pragma once

#include <prism/app/widget_tree.hpp>
#include <prism/ui/access.hpp>

#include <vector>

namespace prism::app {

// The accessibility tree: what an assistive-technology bridge (e.g. AccessKit) publishes.
// Built from the widget tree on the logic thread; pure, so it is tested without any platform.
struct AccessNode {
    ui::AccessInfo info;
    WidgetId id = 0;
    bool focused = false;
    std::vector<AccessNode> children;
};

namespace access_detail {

// Leaves become nodes; layout containers (stacks, splits) are not announced to assistive
// technology, so their children are lifted into the parent.
// simplify: no bounds yet (a bridge needs them for hit testing) and containers that do mean
// something (tables, trees, tab panels) are flattened too; add both with the bridge.
inline void collect(const WidgetNode& node, WidgetId focused, std::vector<AccessNode>& out) {
    if (node.access) {
        AccessNode a{node.access(), node.id, node.id == focused, {}};
        if (a.info.name.empty()) a.info.name = node.name;
        out.push_back(std::move(a));
        return;
    }
    for (const auto& child : node.children) collect(child, focused, out);
}

} // namespace access_detail

inline AccessNode build_access_tree(const WidgetTree& tree) {
    AccessNode root;
    root.info.role = ui::Role::Window;
    access_detail::collect(tree.root(), tree.focused_id(), root.children);
    return root;
}

} // namespace prism::app
