#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Docking (doc/design/drag-and-docking.md, part 2): the layout is a value the app owns, saved
// with its settings, built into splits and tab groups by ViewBuilder::dock.
namespace prism::ui {

// One part of a dock: a group of panels shown as tabs, or a split of two or more parts.
struct DockNode {
    std::vector<std::string> panels{};   // a group: its panels' names, in tab order (empty: a split)
    std::size_t active = 0;              // a group: the panel shown
    bool vertical = false;               // a split: parts above one another (otherwise side by side)
    std::vector<float> sizes{};          // a split: the parts' sizes along its axis (empty: equal)
    std::vector<DockNode> parts{};       // a split's parts
    bool operator==(const DockNode&) const = default;
};

struct DockLayout {
    DockNode root{};
    bool operator==(const DockLayout&) const = default;
};

inline DockNode dock_group(std::vector<std::string> panels) { return {.panels = std::move(panels)}; }

inline DockNode dock_split(bool vertical, std::vector<DockNode> parts, std::vector<float> sizes = {}) {
    return {.vertical = vertical, .sizes = std::move(sizes), .parts = std::move(parts)};
}

inline bool is_group(const DockNode& n) { return n.parts.empty(); }

// The same parts with the same panels in the same order: only sizes or shown tabs differ, which
// the built dock follows without being rebuilt.
inline bool same_structure(const DockNode& a, const DockNode& b) {
    if (a.panels != b.panels || a.vertical != b.vertical || a.parts.size() != b.parts.size()) return false;
    for (std::size_t i = 0; i < a.parts.size(); ++i)
        if (!same_structure(a.parts[i], b.parts[i])) return false;
    return true;
}

namespace dock_detail {
// Drops panels not in `names` (and remembers the ones seen); then empty groups disappear and a
// split left with one part becomes that part.
inline bool prune(DockNode& n, const std::vector<std::string>& names, std::vector<std::string>& seen) {
    if (is_group(n)) {
        std::erase_if(n.panels, [&](const std::string& p) {
            bool keep = std::ranges::contains(names, p) && !std::ranges::contains(seen, p);
            if (keep) seen.push_back(p);
            return !keep;
        });
        n.active = std::min(n.active, n.panels.empty() ? 0 : n.panels.size() - 1);
        return !n.panels.empty();
    }
    std::vector<DockNode> kept;
    std::vector<float> kept_sizes;
    for (std::size_t i = 0; i < n.parts.size(); ++i)
        if (prune(n.parts[i], names, seen)) {
            kept.push_back(std::move(n.parts[i]));
            if (i < n.sizes.size()) kept_sizes.push_back(n.sizes[i]);
        }
    n.parts = std::move(kept);
    n.sizes = kept_sizes.size() == n.parts.size() ? std::move(kept_sizes) : std::vector<float>{};
    if (n.parts.size() == 1) n = DockNode{std::move(n.parts.front())};
    return !n.parts.empty() || !n.panels.empty();
}

inline DockNode& first_group(DockNode& n) { return is_group(n) ? n : first_group(n.parts.front()); }
} // namespace dock_detail

// The layout fitted to the panels the app has now (`names`, in its order): a saved layout from
// another version always gives a working dock. Unknown panels are dropped; missing ones join
// the first group.
inline DockLayout fitted(DockLayout layout, const std::vector<std::string>& names) {
    std::vector<std::string> seen;
    if (!dock_detail::prune(layout.root, names, seen)) layout.root = DockNode{};
    auto& first = dock_detail::first_group(layout.root);
    for (const auto& name : names)
        if (!std::ranges::contains(seen, name)) first.panels.push_back(name);
    return layout;
}

} // namespace prism::ui
