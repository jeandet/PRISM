#pragma once

#include <prism/ui/node.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
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

// Where a floating panel's card sits over the dock, where no window can open (the browser): in
// the dock's coordinates.
struct PanelPlace {
    std::string name;
    float x = 0.f, y = 0.f;
    float width = 320.f, height = 240.f;
    bool operator==(const PanelPlace&) const = default;
};

struct DockLayout {
    DockNode root{};
    std::vector<std::string> floating{};   // panels in windows of their own (cards in the browser)
    std::vector<std::string> closed{};     // panels the user closed: shown again with show_panel
    std::vector<PanelPlace> places{};      // floating panels' cards, once moved
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

namespace dock_detail {
// The panels of `list` the app has, not already in `taken`, once each; each kept is taken.
inline std::vector<std::string> known(const std::vector<std::string>& list, const std::vector<std::string>& names,
                                      std::vector<std::string>& taken) {
    std::vector<std::string> out;
    for (auto& p : list)
        if (std::ranges::contains(names, p) && !std::ranges::contains(taken, p)) {
            out.push_back(p);
            taken.push_back(p);
        }
    return out;
}
} // namespace dock_detail

// The layout fitted to the panels the app has now (`names`, in its order): a saved layout from
// another version always gives a working dock. Unknown panels are dropped; missing ones (not
// docked, floating or closed) join the first group.
inline DockLayout fitted(DockLayout layout, const std::vector<std::string>& names) {
    std::vector<std::string> seen;
    if (!dock_detail::prune(layout.root, names, seen)) layout.root = DockNode{};
    layout.floating = dock_detail::known(layout.floating, names, seen);
    layout.closed = dock_detail::known(layout.closed, names, seen);
    std::erase_if(layout.places, [&](const PanelPlace& p) { return !std::ranges::contains(layout.floating, p.name); });
    auto& first = dock_detail::first_group(layout.root);
    for (const auto& name : names)
        if (!std::ranges::contains(seen, name)) first.panels.push_back(name);
    return layout;
}

// Where a panel dropped on a group goes: into it (the middle), or beside it (an edge).
enum class DockZone { Middle, Left, Right, Top, Bottom };

namespace dock_detail {
inline Rect square(Point center, float side) {
    return {Point{X{center.x.raw() - side / 2.f}, Y{center.y.raw() - side / 2.f}},
            Size{Width{side}, Height{side}}};
}

// The side of a guide button: about 32 px, and smaller in an area too small for the cross.
inline float button_side(Size area) { return std::min({32.f, area.w.raw() / 3.5f, area.h.raw() / 3.5f}); }

// The middle, or the edge the drop is nearest to within a quarter of the group's size.
inline DockZone nearest_edge(DropPlace at) {
    float x = at.at.x.raw() / std::max(1.f, at.size.w.raw()), y = at.at.y.raw() / std::max(1.f, at.size.h.raw());
    float edge = std::min({x, 1.f - x, y, 1.f - y});
    if (edge > 0.25f) return DockZone::Middle;
    if (edge == x) return DockZone::Left;
    if (edge == 1.f - x) return DockZone::Right;
    return edge == y ? DockZone::Top : DockZone::Bottom;
}
} // namespace dock_detail

// The guide a group shows while a panel is dragged over it: five buttons in a cross at its centre
// (the middle, then Left, Right, Top and Bottom), in the group's own coordinates. Dropping on one
// does what dropping on that zone of the group does.
inline std::array<std::pair<DockZone, Rect>, 5> dock_guide(Size group) {
    float b = dock_detail::button_side(group);
    float step = b + b / 8.f;
    Point c{X{group.w.raw() / 2.f}, Y{group.h.raw() / 2.f}};
    auto at = [&](float dx, float dy) { return dock_detail::square(Point{X{c.x.raw() + dx}, Y{c.y.raw() + dy}}, b); };
    return {{{DockZone::Middle, at(0.f, 0.f)},
             {DockZone::Left, at(-step, 0.f)},
             {DockZone::Right, at(step, 0.f)},
             {DockZone::Top, at(0.f, -step)},
             {DockZone::Bottom, at(0.f, step)}}};
}

// The guide a dock shows on its outer edges: four buttons at the middle of each side, inset from
// it, in the dock's own coordinates. A drop there docks the panel along that whole side.
inline std::array<std::pair<DockZone, Rect>, 4> dock_edge_guide(Size dock) {
    float b = dock_detail::button_side(dock);
    float inset = std::min(8.f, b / 4.f);
    float x0 = inset + b / 2.f, x1 = dock.w.raw() - inset - b / 2.f;
    float y0 = inset + b / 2.f, y1 = dock.h.raw() - inset - b / 2.f;
    float cx = dock.w.raw() / 2.f, cy = dock.h.raw() / 2.f;
    return {{{DockZone::Left, dock_detail::square(Point{X{x0}, Y{cy}}, b)},
             {DockZone::Right, dock_detail::square(Point{X{x1}, Y{cy}}, b)},
             {DockZone::Top, dock_detail::square(Point{X{cx}, Y{y0}}, b)},
             {DockZone::Bottom, dock_detail::square(Point{X{cx}, Y{y1}}, b)}}};
}

// The edge button of a dock at `at` (a drop anywhere else on the dock is not one).
inline std::optional<DockZone> dock_edge_zone(DropPlace at) {
    for (auto& [zone, r] : dock_edge_guide(at.size))
        if (r.contains(at.at)) return zone;
    return std::nullopt;
}

// Where the drop takes the panel: a guide button under it, otherwise the group's nearest edge.
inline DockZone dock_zone(DropPlace at) {
    for (auto& [zone, r] : dock_guide(at.size))
        if (r.contains(at.at)) return zone;
    return dock_detail::nearest_edge(at);
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

// The layout with panel `name` put into the group holding `anchor`, before its tab `index` (its
// tab count or more: after the last), and shown there: a reorder within its own group, a move
// from another. Groups left empty go, as in move_panel.
inline DockLayout insert_panel(DockLayout layout, const std::string& name, const std::string& anchor, std::size_t index) {
    auto* from = dock_detail::group_of(layout.root, name);
    auto* to = dock_detail::group_of(layout.root, anchor);
    if (!from || !to) return layout;
    const auto& tabs = to->panels;
    if ((index < tabs.size() && tabs[index] == name) || (index > 0 && index <= tabs.size() && tabs[index - 1] == name))
        return layout;   // where it is already
    const std::string before = index < tabs.size() ? tabs[index] : std::string{};
    auto other = std::ranges::find_if(tabs, [&](const std::string& p) { return p != name; });
    if (other == tabs.end()) return layout;
    const std::string kept = *other;   // a panel of the target group that stays in it
    std::erase(from->panels, name);
    std::vector<std::string> left, seen;
    dock_detail::names_in(layout.root, left);
    dock_detail::prune(layout.root, left, seen);
    auto& target = *dock_detail::group_of(layout.root, kept);
    auto at = before.empty() ? target.panels.end() : std::ranges::find(target.panels, before);
    target.active = static_cast<std::size_t>(at - target.panels.begin());
    target.panels.insert(at, name);
    return layout;
}

// The layout with panel `name` docked along the whole side `side` of the dock: out of its group
// (pruned, as a move does), into a group of its own that the root is split with.
inline DockLayout dock_panel_at_edge(DockLayout layout, const std::string& name, DockZone side) {
    auto* from = dock_detail::group_of(layout.root, name);
    if (!from || side == DockZone::Middle) return layout;
    if (is_group(layout.root) && layout.root.panels.size() == 1) return layout;   // nothing to split it with
    std::erase(from->panels, name);
    std::vector<std::string> left, seen;
    dock_detail::names_in(layout.root, left);
    dock_detail::prune(layout.root, left, seen);   // drops the emptied group, collapses its split
    bool vertical = side == DockZone::Top || side == DockZone::Bottom;
    bool first = side == DockZone::Left || side == DockZone::Top;
    DockNode moved = dock_group({name});
    DockNode rest = std::move(layout.root);
    layout.root = first ? dock_split(vertical, {std::move(moved), std::move(rest)})
                        : dock_split(vertical, {std::move(rest), std::move(moved)});
    return layout;
}

// The layout with panel `name` taken out of its group and floating, in a window of its own.
inline DockLayout float_panel(DockLayout layout, const std::string& name) {
    auto* from = dock_detail::group_of(layout.root, name);
    if (!from || std::ranges::contains(layout.floating, name)) return layout;
    std::erase(from->panels, name);
    std::vector<std::string> left, seen;
    dock_detail::names_in(layout.root, left);
    if (!dock_detail::prune(layout.root, left, seen)) layout.root = DockNode{};
    layout.floating.push_back(name);
    return layout;
}

// The layout with panel `name` closed: out of its group or its window, kept in `closed`.
inline DockLayout close_panel(DockLayout layout, const std::string& name) {
    if (auto* from = dock_detail::group_of(layout.root, name)) {
        std::erase(from->panels, name);
        std::vector<std::string> left, seen;
        dock_detail::names_in(layout.root, left);
        if (!dock_detail::prune(layout.root, left, seen)) layout.root = DockNode{};
    } else if (std::erase(layout.floating, name) == 0) {
        return layout;
    }
    if (!std::ranges::contains(layout.closed, name)) layout.closed.push_back(name);
    return layout;
}

// The layout with closed panel `name` shown again, in the first group.
inline DockLayout show_panel(DockLayout layout, const std::string& name) {
    if (std::erase(layout.closed, name) == 0) return layout;
    auto& group = dock_detail::first_group(layout.root);
    group.panels.push_back(name);
    group.active = group.panels.size() - 1;
    return layout;
}

// Where floating panel `name`'s card sits: where it was moved, else stepped down from the
// dock's corner by its place among the floating panels, so cards do not hide one another.
inline PanelPlace card_place(const DockLayout& layout, const std::string& name) {
    if (auto it = std::ranges::find(layout.places, name, &PanelPlace::name); it != layout.places.end()) return *it;
    const auto i = static_cast<float>(std::ranges::find(layout.floating, name) - layout.floating.begin());
    return {.name = name, .x = 40.f + 28.f * i, .y = 40.f + 28.f * i};
}

// The layout with `name`'s card moved to (x, y).
inline DockLayout place_card(DockLayout layout, const std::string& name, float x, float y) {
    auto place = card_place(layout, name);
    place.x = x;
    place.y = y;
    std::erase_if(layout.places, [&](const PanelPlace& p) { return p.name == name; });
    layout.places.push_back(std::move(place));
    return layout;
}

namespace dock_detail {
// Floating panel `name` into the group holding `anchor` (else the first), shown there; false if
// it was not floating.
inline bool unfloat(DockLayout& layout, const std::string& name, const std::string& anchor) {
    if (std::erase(layout.floating, name) == 0) return false;
    std::erase_if(layout.places, [&](const PanelPlace& p) { return p.name == name; });
    auto* to = group_of(layout.root, anchor);
    auto& group = to ? *to : first_group(layout.root);
    group.panels.push_back(name);
    group.active = group.panels.size() - 1;
    return true;
}
} // namespace dock_detail

// The layout with floating panel `name` docked by `anchor`'s group: into it (Middle) or beside
// it (an edge), as move_panel puts a panel.
inline DockLayout dock_floating(DockLayout layout, const std::string& name, const std::string& anchor, DockZone zone) {
    if (!dock_detail::unfloat(layout, name, anchor)) return layout;
    return zone == DockZone::Middle ? layout : move_panel(std::move(layout), name, anchor, zone);
}

// The layout with floating panel `name` docked along the whole side `side` of the dock.
inline DockLayout dock_floating_at_edge(DockLayout layout, const std::string& name, DockZone side) {
    if (!dock_detail::unfloat(layout, name, {})) return layout;
    return dock_panel_at_edge(std::move(layout), name, side);
}

// The layout with panel `name` back in the dock (in its first group).
inline DockLayout dock_panel(DockLayout layout, const std::string& name) {
    if (std::erase(layout.floating, name) == 0) return layout;
    dock_detail::first_group(layout.root).panels.push_back(name);
    return layout;
}

} // namespace prism::ui
