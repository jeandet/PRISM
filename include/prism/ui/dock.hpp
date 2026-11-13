#pragma once

#include <prism/ui/node.hpp>

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

// Where a panel dropped on a group goes: into it (the middle), or beside it (an edge).
enum class DockZone { Middle, Left, Right, Top, Bottom };

// The middle, or the edge the drop is nearest to within a quarter of the group's size.
inline DockZone dock_zone(DropPlace at) {
    float x = at.at.x.raw() / std::max(1.f, at.size.w.raw()), y = at.at.y.raw() / std::max(1.f, at.size.h.raw());
    float edge = std::min({x, 1.f - x, y, 1.f - y});
    if (edge > 0.25f) return DockZone::Middle;
    if (edge == x) return DockZone::Left;
    if (edge == 1.f - x) return DockZone::Right;
    return edge == y ? DockZone::Top : DockZone::Bottom;
}

namespace dock_detail {
inline DockNode* group_of(DockNode& n, const std::string& panel) {
    if (is_group(n)) return std::ranges::contains(n.panels, panel) ? &n : nullptr;
    for (auto& p : n.parts)
        if (auto* g = group_of(p, panel)) return g;
    return nullptr;
}

inline void names_in(const DockNode& n, std::vector<std::string>& out) {
    out.insert(out.end(), n.panels.begin(), n.panels.end());
    for (auto& p : n.parts) names_in(p, out);
}
} // namespace dock_detail

// The layout with panel `name` moved next to panel `anchor`: into anchor's group (Middle,
// shown there), or into a new group on one side of it (an edge). Groups left empty go, and
// splits left with one part become that part.
inline DockLayout move_panel(DockLayout layout, const std::string& name, std::string anchor, DockZone zone) {
    auto* from = dock_detail::group_of(layout.root, name);
    auto* to = dock_detail::group_of(layout.root, anchor);
    if (!from || !to) return layout;
    if (from == to) {
        if (zone == DockZone::Middle || from->panels.size() == 1) return layout;   // already there
        anchor = *std::ranges::find_if(from->panels, [&](const std::string& p) { return p != name; });
    }
    std::erase(from->panels, name);
    std::vector<std::string> left, seen;
    dock_detail::names_in(layout.root, left);
    dock_detail::prune(layout.root, left, seen);   // drops the emptied group, collapses its split
    auto& target = *dock_detail::group_of(layout.root, anchor);
    if (zone == DockZone::Middle) {
        target.panels.push_back(name);
        target.active = target.panels.size() - 1;
        return layout;
    }
    bool vertical = zone == DockZone::Top || zone == DockZone::Bottom;
    bool first = zone == DockZone::Left || zone == DockZone::Top;
    DockNode moved = dock_group({name});
    DockNode kept = std::move(target);
    target = first ? dock_split(vertical, {std::move(moved), std::move(kept)})
                   : dock_split(vertical, {std::move(kept), std::move(moved)});
    return layout;
}

} // namespace prism::ui
