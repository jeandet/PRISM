#pragma once

// Included at the end of app/view_builder.hpp: builds a dock (doc/design/drag-and-docking.md)
// through the builder's public API (stacks, tabs, splits, drag roles), and defines
// ViewBuilder::dock, which places the container these build into.

#include <prism/ui/dock.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace prism::app::dock_builder {

using DockPanel = ViewBuilder::DockPanel;

// What a dock keeps between builds: its panels, the layout it built last, and the fields its
// tab groups and splits use, with the links writing their changes back into the layout.
struct DockState {
    std::vector<std::string> names;
    DockNode built;
    std::deque<Field<TabBar<>>> tabs;        // a deque: fields never move once made
    std::deque<Field<SplitSizes>> splits;
    std::vector<Connection> links;
    std::vector<std::string> floating;            // as built last
    std::map<std::string, WindowId> windows;      // floating panels' windows (0: asked for, not open yet)
};

// What dragging a dock's tab carries: which dock, which panel.
struct PanelDrag {
    const void* dock;
    std::string name;
};

// The node at `path` (child indices from the root).
inline DockNode& node_at(DockNode& root, const std::vector<std::size_t>& path) {
    DockNode* n = &root;
    for (auto i : path) n = &n->parts[i];
    return *n;
}

inline const DockPanel& panel(const std::vector<DockPanel>& panels, const std::string& name) {
    return *std::ranges::find(panels, name, &DockPanel::name);
}

// What dragging a floating card's title bar carries: which dock, which panel, and where in the
// card it was pressed (so it lands where the pointer lets it go).
struct CardDrag {
    const void* dock;
    std::string name;
    Point grab;
};

// The key a panel's built widgets are kept under: in a group, in a card, wherever it moves.
inline std::string panel_key(const void* dock_id, const std::string& name) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(dock_id)) + "/" + name;
}

// Where the dock's parts write the user's changes (a shown tab, split sizes): the layout's node at
// `path`, the layout fitted first so that paths agree with what was built.
struct Place {
    DockState& state;
    const std::vector<DockPanel>& panels;
    Field<DockLayout>& layout;
    WidgetTree& tree;
    std::vector<std::size_t> path;

    void write_back(auto edit) const {
        auto l = fitted(layout.get(), state.names);
        edit(node_at(l.root, path));
        layout.set(std::move(l));
    }
    Place at(std::size_t part) const {
        auto sub = path;
        sub.push_back(part);
        return {state, panels, layout, tree, std::move(sub)};
    }
};

// A group takes a panel dropped on its middle (into its tabs) or an edge (a new group beside it).
inline DragRole group_drop_role(const Place& where, std::string anchor) {
    const void* dock_id = &where.state;
    return {.accepts = [dock_id](const std::any& v) {
                auto* p = std::any_cast<PanelDrag>(&v);
                return p && p->dock == dock_id;
            },
            .drop = [&state = where.state, &layout = where.layout, anchor = std::move(anchor)](const std::any& v, DropPlace at) {
                auto& name = std::any_cast<const PanelDrag&>(v).name;
                layout.set(move_panel(fitted(layout.get(), state.names), name, anchor, dock_zone(at)));
            },
            .zones = true};
}

// The dock's own container: four buttons at the middle of its edges dock a panel along that
// whole side of the dock (a new top-level split). Elsewhere on it, the group under it takes the
// drop: `claims` is only true over one of those buttons.
inline DragRole dock_outer_role(DockState& state, Field<DockLayout>& layout) {
    const void* dock_id = &state;
    return {.accepts = [dock_id](const std::any& v) {
                auto* p = std::any_cast<PanelDrag>(&v);
                return p && p->dock == dock_id;
            },
            .drop = [&state, &layout](const std::any& v, DropPlace at) {
                auto& name = std::any_cast<const PanelDrag&>(v).name;
                if (auto side = dock_edge_zone(at))
                    layout.set(dock_panel_at_edge(fitted(layout.get(), state.names), name, *side));
            },
            .edge_zones = true,
            .claims = [](DropPlace at) { return dock_edge_zone(at).has_value(); }};
}

// A group: its panels as tabs, every group a lone panel too, since a tab is what the user drags
// to move a panel. A panel is kept under the dock and its name, so a moved one keeps its widgets;
// a tab let go outside the window floats its panel.
inline void place_group(ViewBuilder& vb, const Place& where, const DockNode& node) {
    auto& bar = where.state.tabs.emplace_back(TabBar<>{.selected = node.active});
    where.state.links.push_back(bar.on_change().connect([where](const TabBar<>& b) {
        where.write_back([&](DockNode& n) { n.active = b.selected; });
    }));
    const void* dock_id = &where.state;
    auto names = node.panels;
    vb.with_drag(group_drop_role(where, names.front()), [&] {
        vb.tabs(bar, [&] {
            for (auto& name : node.panels) {
                auto& p = panel(where.panels, name);
                auto key = panel_key(dock_id, name);
                vb.tab(p.title, [build = p.build, key](ViewBuilder& inner) {
                    inner.keep_as(key, [&] { inner.vstack([&] { build(inner); }); });
                });
            }
        }, {.source = [dock_id, names](std::size_t i, Point) {
                return i < names.size() ? std::any(PanelDrag{dock_id, names[i]}) : std::any{};
            },
            .outside = [&state = where.state, &layout = where.layout, &tree = where.tree](const std::any& v) {
                if (!tree.can_open_windows()) return;
                auto& name = std::any_cast<const PanelDrag&>(v).name;
                layout.set(float_panel(fitted(layout.get(), state.names), name));
            },
            .accepts = [dock_id](const std::any& v) {
                auto* p = std::any_cast<PanelDrag>(&v);
                return p && p->dock == dock_id;
            },
            .drop = [&state = where.state, &layout = where.layout, anchor = names.front()](const std::any& v, std::size_t i) {
                auto& name = std::any_cast<const PanelDrag&>(v).name;
                layout.set(insert_panel(fitted(layout.get(), state.names), name, anchor, i));
            },
            .closable = [names, &panels = where.panels](std::size_t i) {
                return i < names.size() && panel(panels, names[i]).closable;
            },
            .close = [names, &state = where.state, &layout = where.layout](std::size_t i) {
                if (i < names.size()) layout.set(close_panel(fitted(layout.get(), state.names), names[i]));
            },
            .detach = [names, &state = where.state, &layout = where.layout](std::size_t i) {
                if (i < names.size()) layout.set(float_panel(fitted(layout.get(), state.names), names[i]));
            }});
    });
}

inline void place_part(ViewBuilder& vb, const Place& where, const DockNode& node);

// Where no window can open (the browser): a card's title bar dropped anywhere on the dock moves
// the card there.
inline DragRole card_move_role(DockState& state, Field<DockLayout>& layout) {
    const void* dock_id = &state;
    return {.accepts = [dock_id](const std::any& v) {
                auto* c = std::any_cast<CardDrag>(&v);
                return c && c->dock == dock_id;
            },
            .drop = [&state, &layout](const std::any& v, DropPlace at) {
                auto& c = std::any_cast<const CardDrag&>(v);
                layout.set(place_card(fitted(layout.get(), state.names), c.name, at.at.x.raw() - c.grab.x.raw(),
                                      at.at.y.raw() - c.grab.y.raw()));
            }};
}

// A floating panel as a card over the dock: its title on a tab bar to drag it by, with the
// button docking it back (and a close button, if it closes), over the panel itself, kept under
// the same key as in a group.
inline void place_card_of(ViewBuilder& vb, const Place& where, const std::string& name) {
    auto& p = panel(where.panels, name);
    auto& bar = where.state.tabs.emplace_back(TabBar<>{});
    const void* dock_id = &where.state;
    const auto inset = frame_insets(Framing{FrameLook::Card});
    auto again = [&state = where.state, &layout = where.layout, name](auto change) {
        return [&state, &layout, name, change](std::size_t) { layout.set(change(fitted(layout.get(), state.names), name)); };
    };
    vb.card([&] {
        vb.tabs(bar, [&] {
            vb.tab(p.title, [build = p.build, key = panel_key(dock_id, name)](ViewBuilder& inner) {
                inner.keep_as(key, [&] { inner.vstack([&] { build(inner); }); });
            });
        }, {.source = [dock_id, name, inset](std::size_t, Point at) {
                return std::any(CardDrag{dock_id, name, Point{at.x + DX{inset.left}, at.y + DY{inset.top}}});
            },
            .closable = [closable = p.closable](std::size_t) { return closable; },
            .close = again([](DockLayout l, const std::string& n) { return close_panel(std::move(l), n); }),
            .detach = again([](DockLayout l, const std::string& n) { return dock_panel(std::move(l), n); })});
    });
}

// The dock with its floating panels as cards over it, each where its place says.
inline void place_with_cards(ViewBuilder& vb, const Place& where, const DockNode& root,
                             const std::vector<std::string>& floating) {
    vb.with_drag(card_move_role(where.state, where.layout), [&] {
        vb.layers([&layout = where.layout, floating] {
            std::vector<Rect> out;
            const auto& l = layout.get();
            for (auto& name : floating) {
                auto p = card_place(l, name);
                out.push_back(Rect{Point{X{p.x}, Y{p.y}}, Size{Width{p.width}, Height{p.height}}});
            }
            return out;
        }, [&] {
            place_part(vb, where, root);
            for (auto& name : floating) place_card_of(vb, where, name);
        });
    });
}

// A split: its parts side by side or above one another, with handles between them, the sizes
// the user drags written back.
inline void place_split(ViewBuilder& vb, const Place& where, const DockNode& node) {
    auto& sizes = where.state.splits.emplace_back(SplitSizes{node.sizes});
    where.state.links.push_back(sizes.on_change().connect([where](const SplitSizes& s) {
        where.write_back([&](DockNode& n) { n.sizes = s.value; });
    }));
    auto parts = [&] {
        vb.remember_split(sizes);
        for (std::size_t i = 0; i < node.parts.size(); ++i) {
            if (i > 0) vb.handle();
            place_part(vb, where.at(i), node.parts[i]);
        }
    };
    if (node.vertical) vb.vstack(parts);
    else vb.hstack(parts);
}

inline void place_part(ViewBuilder& vb, const Place& where, const DockNode& node) {
    if (is_group(node) && node.panels.empty()) vb.vstack([] {});   // every panel floats
    else if (is_group(node)) place_group(vb, where, node);
    else place_split(vb, where, node);
}

// Opens a window for each floating panel that has none, and closes the windows of panels docked
// again. A window that cannot open (no opener: the browser, tests), or that the user closes,
// puts its panel back in the dock.
inline void float_windows(WidgetTree& tree, const std::shared_ptr<DockState>& state,
                          const std::vector<DockPanel>& panels, Field<DockLayout>& layout) {
    for (auto it = state->windows.begin(); it != state->windows.end();)
        if (std::ranges::contains(state->floating, it->first)) ++it;
        else {
            if (it->second) tree.close_window(it->second);
            it = state->windows.erase(it);
        }
    for (auto& name : state->floating) {
        if (state->windows.contains(name)) continue;
        state->windows[name] = 0;
        auto dock_again = [state, &layout, name] {
            state->windows.erase(name);
            layout.set(dock_panel(fitted(layout.get(), state->names), name));
        };
        auto& p = panel(panels, name);
        tree.request_window({.title = p.title, .build = p.build,
                             .on_opened = [state, name, dock_again](WindowId id) {
                                 if (id == 0) dock_again();
                                 else state->windows[name] = id;
                             },
                             .on_closed = dock_again});
    }
}

} // namespace prism::app::dock_builder

namespace prism::app {

// The layout is the app's value, kept in its settings; the user's split sizes and tab choices are
// written back into it. Panels the layout does not name join its first group, names with no
// panel are dropped. The content is built when the layout's structure changes (build_content).
inline void ViewBuilder::dock(Field<DockLayout>& layout, std::vector<DockPanel> panels) {
    placed_.insert(&layout);
    auto state = std::make_shared<dock_builder::DockState>();
    for (auto& p : panels) {
        state->names.push_back(p.name);
        // Built later, when the dock builds: run once now, as popups do, to learn the fields it
        // places (they count as placed by the view).
        Node scratch;
        ViewBuilder probe{tree_, scratch};
        p.build(probe);
        placed_.insert(probe.placed().begin(), probe.placed().end());
    }
    auto shared_panels = std::make_shared<std::vector<DockPanel>>(std::move(panels));
    Node c;
    c.id = tree_.next_id_++;
    c.is_leaf = false;
    c.layout_kind = LayoutKind::Column;
    c.on_change = view_builder_detail::adapt_on_change(layout);   // a change: the next frame checks
    c.content_stale = [state, &layout] {
        auto fit = fitted(layout.get(), state->names);
        return !same_structure(state->built, fit.root) || state->floating != fit.floating;
    };
    c.build_content = [state, shared_panels, &layout, &tree = tree_](Node& target) {
        state->links.clear();
        state->tabs.clear();
        state->splits.clear();
        auto fit = fitted(layout.get(), state->names);
        state->built = fit.root;
        state->floating = fit.floating;
        ViewBuilder vb{tree, target};
        if (tree.can_open_windows()) {
            dock_builder::place_part(vb, {*state, *shared_panels, layout, tree, {}}, state->built);
            dock_builder::float_windows(tree, state, *shared_panels, layout);
        } else {   // the browser: floating panels are cards over the dock
            dock_builder::place_with_cards(vb, {*state, *shared_panels, layout, tree, {}}, state->built, state->floating);
        }
    };
    c.drag = dock_builder::dock_outer_role(*state, layout);
    current_parent().children.push_back(std::move(c));
}

} // namespace prism::app
