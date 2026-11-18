#pragma once

// Include <prism/app/widget_tree.hpp> instead of this header directly. ViewBuilder's inline
// method bodies call WidgetTree's methods and need it complete, which only happens because
// widget_tree.hpp includes this file at its own bottom, after WidgetTree's definition closes --
// including this header on its own leaves WidgetTree an incomplete forward declaration and
// fails with "invalid use of incomplete type 'class prism::app::WidgetTree'".

#include <prism/delegates/menu_delegates.hpp>
#include <prism/delegates/tabs_delegates.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/core/list.hpp>
#include <prism/core/traits.hpp>
#include <prism/text/editor.hpp>
#include <prism/text/view.hpp>
#include <prism/ui/layout.hpp>
#include <prism/ui/table.hpp>
#include <prism/ui/dock.hpp>
// Kept for the tree/example code that names TreeController/TreeRow/TreeNodeId without including
// it (the module headers used to provide it transitively). Only tree()'s declaration needs it
// here, and that would take a forward declaration -- dropping it means adding the include to
// those files first.
#include <prism/ui/tree.hpp>
#include <prism/ui/widget_node.hpp>
#if __cpp_impl_reflection
#include <prism/core/reflect.hpp>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <set>
#include <type_traits>
#include <vector>

namespace prism::nodes { class Graph; class Port; }

namespace prism::app {
class WidgetTree;
struct NodeBoardParts;

using namespace prism::core;
using namespace prism::ui;

namespace view_builder_detail {

// A block's type name, for the outline that shows it (WidgetTree::show_blocks); without
// reflection every block is just "block".
template <typename C>
std::string_view block_name() {
#if __cpp_impl_reflection
    static constexpr std::string_view name =
        std::meta::has_identifier(^^C) ? std::meta::identifier_of(^^C) : std::string_view{"block"};
    return name;
#else
    return "block";
#endif
}

inline bool is_bar(const Node& n) {
    return n.frame && (n.frame->look == FrameLook::Toolbar || n.frame->look == FrameLook::StatusBar);
}

// Shared by list(), tree() and table(List<T>&): adapt a List<T>'s three signals
// into the Node's vlist_on_* connector slots (identical shape at every call site).
template <typename T>
void wire_list_signals(Node& container, List<T>& items) {
    container.vlist_on_insert = [&items](size_t, std::function<void()> cb) -> Connection {
        return items.on_insert().connect(
            [cb = std::move(cb)](size_t, const auto&) { cb(); });
    };
    container.vlist_on_remove = [&items](size_t, std::function<void()> cb) -> Connection {
        return items.on_remove().connect(
            [cb = std::move(cb)](size_t) { cb(); });
    };
    container.vlist_on_update = [&items](size_t, std::function<void()> cb) -> Connection {
        return items.on_update().connect(
            [cb = std::move(cb)](size_t, const auto&) { cb(); });
    };
}

// A sorted table over a List re-sorts only the rows updated or appended (ui::refresh_order):
// the List reports updated rows, and appends only ever add rows at the end.
template <typename T>
void track_rows(TableState& ts, List<T>& list) {
    ts.rows_tracked = true;
    ts.row_watch.push_back(list.on_update().connect([&ts](size_t row, const T&) {
        if (!ts.sort.get() || ts.order_stale) return;
        ts.changed_rows.push_back(row);
        if (ts.changed_rows.size() > ts.row_count() / 4) ts.order_stale = true;   // a full sort is cheaper
    }));
}

// A list row that shows whether it is `selected`, and selects itself when pressed. A row that
// takes no focus of its own hands it to the list, so the arrow keys reach the list.
void show_selection(WidgetNode& wn, size_t index, Field<std::optional<std::size_t>>& selected,
                    WidgetTree& tree, WidgetId list_id);

// The row a key moves a list's selection to: Up/Down step, Home/End jump; none for other keys.
std::optional<std::size_t> list_key_target(std::optional<std::size_t> selected, std::size_t count,
                                           int32_t key);

// The list itself takes the focus and moves the selection with the keys.
template <typename T>
void select_by_keys(WidgetNode& wn, List<T>& items, Field<std::optional<std::size_t>>& selected, WidgetTree& tree) {
    wn.focus_policy = FocusPolicy::tab_and_click;
    wn.wire = [&items, &selected, &tree](WidgetNode& self) {
        self.connections.push_back(self.on_input.connect([&items, &selected, &tree, &self](const InputEvent& ev) {
            auto* kp = std::get_if<KeyPress>(&ev);
            if (!kp) return;
            auto to = list_key_target(selected.get(), items.size(), kp->key);
            if (!to) return;
            selected.set(*to);
            if (auto* vls = std::any_cast<std::shared_ptr<VirtualListState>>(&self.edit_state))
                tree.scroll_row_into_view(self.id, *to, (*vls)->item_height);
        }));
    };
}

void unbind_vlist_row(WidgetNode& wn);

// Adapts an observable's on_change(SenderHub) into the Node's connector shape,
// used by both the on_change slot and depends_on dependency entries.
template <typename Observable>
auto adapt_on_change(Observable& obs) {
    return [&obs](std::function<void()> cb) -> Connection {
        return obs.on_change().connect([cb = std::move(cb)](const auto&) { cb(); });
    };
}

// A widget is enabled while `flag` holds true: a Field<bool>, a Derived<bool>, or a checkbox
// (ticked: enabled; "Advanced options" enabling the group below it).
template <typename Flag>
Enabler enabler_of(Flag& flag) {
    if constexpr (requires { flag.get().checked; })
        return {[&flag] { return flag.get().checked; }, adapt_on_change(flag)};
    else
        return {[&flag] { return static_cast<bool>(flag.get()); }, adapt_on_change(flag)};
}

// The value type a drop target takes: its callback's first parameter.
template <typename R, typename A, typename... Rest>
A first_param(std::function<R(A, Rest...)>);
template <typename F>
using drop_value_t = std::remove_cvref_t<decltype(first_param(std::function{std::declval<F>()}))>;

// What a row of a reorderable list drags: which list, which row. Only that list takes it.
struct ListRow {
    const void* list;
    std::size_t index;
};

// Each row of `items` drags itself, and takes a row of the same list: dropped on a row's upper
// half it goes before it, on its lower half after it.
template <typename T>
DragRole list_row_drag(List<T>& items, std::size_t i) {
    const void* list = &items;
    return {
        .source = [list, i](const WidgetNode&, Point) { return std::any(ListRow{list, i}); },
        .accepts = [list](const std::any& v) {
            auto* r = std::any_cast<ListRow>(&v);
            return r && r->list == list;
        },
        .drop = [&items, i](const std::any& v, DropPlace at) {
            auto from = std::any_cast<ListRow>(v).index;
            std::size_t to = at.at.y.raw() > at.size.h.raw() / 2 ? i + 1 : i;
            if (to > from) --to;   // the item leaves its own place first
            items.move(from, std::min(to, items.size() - 1));
        },
        .between = true,
    };
}

// What a tree row drags: which tree, which row.
struct TreeRowDrag {
    const TreeController* tree;
    std::size_t row;
};

// Row i of a tree whose controller has on_move: drags itself, takes another row of the same
// tree where TreeController::can_move allows.
DragRole tree_row_drag(TreeController& ctrl, std::size_t i);

template <typename T> struct is_optional : std::false_type {};
template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

inline void add_enabler(Node& node, const Enabler& e) {
    node.enablers.push_back(e);
    for (auto& c : node.children) add_enabler(c, e);
}

// The widgets a dialog is made of (see ViewBuilder::dialog). Created once and changed in place:
// an open dialog's widgets point at them, and the content builder also runs while the view is
// built, so they must never be replaced under it.
struct DialogParts {
    static constexpr float message_wrap_w = 520.f;   // a message wider than this wraps
    static constexpr std::size_t max_message_rows = 10;   // a longer one scrolls

    Field<Label<>> title{{.heading = true}};
    Field<TextArea<>> message{{.read_only = true, .font = FontRole::Ui}};
    std::vector<std::unique_ptr<Field<Button>>> buttons;
    std::vector<Connection> clicks;
    bool syncing = false;   // updating a button's text is not a click

    // The parts as the dialog now says; a new button closes the dialog with its index.
    void sync(Field<Dialog>& d, const TextMeasurer* m);

private:
    void add_button(Field<Dialog>& d);
};

// What dragging a tab of list-backed tabs carries: which list, which item.
struct ListTabDrag {
    const void* list;
    std::size_t index;
};

// List-backed tabs follow their list: any change rebuilds them (`version` counts changes), and
// the selected tab stays on its item -- the one after a closed selected tab, the one before when
// that was the last.
template <typename T>
auto follow_list_tabs(List<T>& items, Field<TabBar<>>& selected, std::shared_ptr<std::uint64_t> version) {
    return std::make_shared<std::array<Connection, 4>>(std::array{
        items.on_insert().connect([version, &items, &selected](std::size_t at, const T&) {
            ++*version;
            const std::size_t s = selected.get().selected;
            if (items.size() > 1 && at <= s) selected.set({.selected = s + 1});
        }),
        items.on_remove().connect([version, &items, &selected](std::size_t gone) {
            ++*version;
            const std::size_t s = selected.get().selected;
            if (s > 0 && (s > gone || s >= items.size())) selected.set({.selected = s - 1});
        }),
        items.on_update().connect([version](std::size_t, const T&) { ++*version; }),
        items.on_move().connect([version, &selected](std::size_t from, std::size_t to) {
            ++*version;
            selected.set({.selected = index_after_move(selected.get().selected, from, to)});
        })});
}

// A change to the list marks the tabs' container for a rebuild.
template <typename T>
std::vector<std::function<Connection(std::function<void()>)>> list_changes(List<T>& items) {
    return {[&items](std::function<void()> cb) { return items.on_insert().connect([cb](std::size_t, const T&) { cb(); }); },
            [&items](std::function<void()> cb) { return items.on_remove().connect([cb](std::size_t) { cb(); }); },
            [&items](std::function<void()> cb) { return items.on_update().connect([cb](std::size_t, const T&) { cb(); }); },
            [&items](std::function<void()> cb) { return items.on_move().connect([cb](std::size_t, std::size_t) { cb(); }); }};
}

} // namespace view_builder_detail

class ViewBuilder {
    WidgetTree& tree_;
    Node& target_;
    std::vector<Node*> stack_;
    std::set<const void*> placed_;
    TabsState* current_tabs_state_ = nullptr;
    NodeBoardParts* board_ = nullptr;   // set while a node_board builds its cards: vb.port records there

    Node& current_parent() {
        return stack_.empty() ? target_ : *stack_.back();
    }

    // Names a leaf after its enclosing field member (tree_.field_names_, populated by
    // build_node_tree()'s reflection walk) instead of node_leaf<T>'s generic value-type
    // fallback -- e.g. "count" instead of "int". No-op when the address isn't a reflected
    // member of any model this tree was built from (block() sub-trees populate their own
    // entries recursively) or when this build lacks reflection support.
    template <typename T>
    void apply_field_name([[maybe_unused]] Node& leaf, [[maybe_unused]] T& observable) {
        if (auto tip = tree_.field_tooltips_.find(static_cast<const void*>(&observable));
            tip != tree_.field_tooltips_.end())
            leaf.tooltip = tip->second;
#if __cpp_impl_reflection
        auto it = tree_.field_names_.find(static_cast<const void*>(&observable));
        if (it == tree_.field_names_.end()) return;
        leaf.name = std::string(it->second);   // accessible name, in every build
#ifdef PRISM_DEBUG_TOOLS_ENABLED
        leaf.debug_name = leaf.name;
#endif
#endif
    }

public:
    template <typename Self>
    struct DependsOnMixin {
        Node& node_ref;
        std::set<const void*>& placed_ref;

        template <typename Observable>
        Self& depends_on(Observable& obs) {
            placed_ref.insert(&obs);   // read by the view, so not forgotten: Field, Derived or Shared
            node_ref.dependencies.push_back(view_builder_detail::adapt_on_change(obs));
            return static_cast<Self&>(*this);
        }

        Self& depends_on(auto&... obs) {
            (depends_on(obs), ...);
            return static_cast<Self&>(*this);
        }
    };

    struct TableBuilder : DependsOnMixin<TableBuilder> {
        TableBuilder& headers(std::vector<std::string> hdrs) {
            if (node_ref.table_state)
                node_ref.table_state->header_overrides = std::move(hdrs);
            return *this;
        }
        // The columns shown, by source index, in screen order (empty: all). The app keeps `shown`
        // (to save it, or to hide columns from a menu); a header drag rewrites it.
        // simplify: no built-in menu to hide columns; the app offers one (see the gallery).
        TableBuilder& columns(Field<std::vector<size_t>>& shown) {
            placed_ref.insert(&shown);
            if (node_ref.table_state) node_ref.table_state->app_columns = &shown;
            return *this;
        }
        // The selected rows, as source row indices, ascending: the user's clicks land in `rows`,
        // and setting `rows` selects them. The table redraws when the app sets it.
        TableBuilder& selection(Field<std::vector<size_t>>& rows) {
            placed_ref.insert(&rows);
            node_ref.dependencies.push_back(view_builder_detail::adapt_on_change(rows));
            if (auto ts = node_ref.table_state) {
                ts->app_selection = &rows;
                ts->app_selection_watch = rows.on_change().connect([raw = ts.get()](const std::vector<size_t>& r) {
                    if (raw->selected_row.get() && !std::ranges::binary_search(r, *raw->selected_row.get()))
                        raw->selected_row.set(r.empty() ? std::nullopt : std::optional{r.front()});
                    if (raw->anchor_row && !std::ranges::binary_search(r, *raw->anchor_row)) raw->anchor_row.reset();
                });
            }
            return *this;
        }
        // Initial sort; the user changes it by clicking a header.
        TableBuilder& sort(size_t column, bool descending = false) {
            if (node_ref.table_state)
                node_ref.table_state->sort.set(SortSpec{column, descending});
            return *this;
        }
    };

    struct CanvasHandle : DependsOnMixin<CanvasHandle> {
        CanvasHandle& min_size(Height h) { node_ref.canvas_min_height = h.raw(); return *this; }
        CanvasHandle& min_size(Width w) { node_ref.canvas_min_width = w.raw(); return *this; }
    };

    // Returned by text_view(), text_editor() and code_editor(): a text fills the space it is given,
    // or, with rows(n), is n lines tall (inside a scroll area, where there is no space to fill).
    struct TextHandle {
        Node& node_ref;
        std::set<const void*>& placed_ref;
        std::shared_ptr<text::View> view;
        std::shared_ptr<text::Editor> editor{};   // null for a text_view
        TextHandle&& rows(int n) && {
            node_ref.canvas_min_height = static_cast<float>(n) * text_line_estimate + 2.f * text_padding;
            return std::move(*this);
        }
        TextHandle&& tooltip(std::string text) && { node_ref.tooltip = std::move(text); return std::move(*this); }
        // Ranges the app highlights (a SQL error's token, search matches), redrawn as they change.
        TextHandle&& highlights(Field<std::vector<text::Range>>& ranges) && {
            placed_ref.insert(&ranges);
            node_ref.dependencies.push_back(view_builder_detail::adapt_on_change(ranges));
            view->decorations = &ranges;
            return std::move(*this);
        }
        TextHandle&& focus_on_open() && { node_ref.focus_on_open = true; return std::move(*this); }
        // Completion (text/complete.hpp): a list under the caret as a word is typed, or on
        // Ctrl+Space, ranked against it. `source` lists what could go at the caret; or always
        // these words. An editor's only: a text_view is not typed in.
        TextHandle&& complete(text::Completer source) && {
            assert(editor && ".complete() is for an editor, not a text_view");
            if (editor) editor->set_completer(std::move(source));
            return std::move(*this);
        }
        TextHandle&& complete(std::vector<std::string> words) && {
            return std::move(*this).complete(text::from_list(std::move(words)));
        }
        // The default text's line height (1.4 x 14 pt) and padding, before any font is measured.
        static constexpr float text_line_estimate = 1.4f * 14.f;
        static constexpr float text_padding = 8.f;
    };

    // Returned by widget(): settings for the leaf just placed. Use it in the same statement --
    // the next placement may move the node it points to.
    struct LeafHandle {
        Node& node_ref;
        std::set<const void*>& placed_ref;   // a flag field counts as used by the view
        // &&: only on the temporary widget() returns, so a stored handle cannot dangle.
        LeafHandle&& tooltip(std::string text) && { node_ref.tooltip = std::move(text); return std::move(*this); }
        // Inside a popup: this widget, not the first one, has the focus when the popup opens
        // (a dialog's default button, a form's most useful field).
        LeafHandle&& focus_on_open() && { node_ref.focus_on_open = true; return std::move(*this); }
        // A drag source (doc/design/drag-and-docking.md): pressing it and moving drags the value
        // `make` returns (any type; a std::optional that is empty: nothing to drag this time).
        template <typename F>
        LeafHandle&& drag_source(F make) && {
            node_ref.drag.source = [make = std::move(make)](const WidgetNode&, Point) -> std::any {
                auto v = make();
                if constexpr (view_builder_detail::is_optional<decltype(v)>::value)
                    return v ? std::any(std::move(*v)) : std::any{};
                else
                    return std::any(std::move(v));
            };
            return std::move(*this);
        }
        // A drop target: `on_drop(const T& value, DropPlace at)` takes dragged values of type T,
        // `at` in this widget's coordinates. A value of another type is refused.
        template <typename F>
        LeafHandle&& drop_target(F on_drop) && {
            using T = view_builder_detail::drop_value_t<F>;
            node_ref.drag.accepts = [](const std::any& v) { return v.type() == typeid(T); };
            node_ref.drag.drop = [on_drop = std::move(on_drop)](const std::any& v, DropPlace at) {
                on_drop(std::any_cast<const T&>(v), at);
            };
            return std::move(*this);
        }
        // Greyed out and deaf to input while `flag` (a bool Field or Derived) is false.
        template <typename Flag>
        LeafHandle&& enabled(Flag& flag) && {
            placed_ref.insert(&flag);
            view_builder_detail::add_enabler(node_ref, view_builder_detail::enabler_of(flag));
            return std::move(*this);
        }
    };

public:
    ViewBuilder(WidgetTree& tree, Node& target)
        : tree_(tree), target_(target) {}

    // The tooltip `observable`'s widget gets wherever it is placed, by widget() or a stack --
    // for code that does not place the widget itself (automatic layouts, bindings). Read when
    // the widget is placed: declare it before.
    template <typename Obs>
    void tooltip_for(Obs& observable, std::string text) {
        tree_.field_tooltips_[static_cast<const void*>(&observable)] = std::move(text);
    }

    template <typename T>
    LeafHandle widget(Field<T>& field) {
        placed_.insert(&field);
        auto leaf = node_leaf(field, tree_.next_id_);
        apply_field_name(leaf, field);
        current_parent().children.push_back(std::move(leaf));
        return {current_parent().children.back(), placed_};
    }

    // Everything `fn` places, nested containers included, is greyed out and deaf to input while
    // `flag` (a bool Field or Derived) is false. Adds no container: the layout is unchanged.
    template <typename Flag>
    void enabled_if(Flag& flag, std::invocable auto&& fn) {
        auto& parent = current_parent();
        auto first = parent.children.size();
        fn();
        placed_.insert(&flag);
        auto e = view_builder_detail::enabler_of(flag);
        for (auto i = first; i < parent.children.size(); ++i)
            view_builder_detail::add_enabler(parent.children[i], e);
    }

    // The current split container (a row or column with handles) starts at the pane sizes in
    // `sizes` and writes them back when the user drags a handle; keep the field in the app's
    // settings to reopen as the user left it.
    void remember_split(Field<SplitSizes>& sizes);

    // What `fn` places (at this level) is kept when rebuilt elsewhere, under `key` (Node::keep_key).
    void keep_as(const std::string& key, std::invocable auto&& fn) {
        auto& parent = current_parent();
        auto first = parent.children.size();
        fn();
        for (auto i = first; i < parent.children.size(); ++i) parent.children[i].keep_key = key;
    }

    // Gives the widgets `fn` places (at this level) the drag role `role`: what LeafHandle's
    // drag_source / drop_target set, spelled out, for bindings that build it themselves.
    void with_drag(const DragRole& role, std::invocable auto&& fn) {
        auto& parent = current_parent();
        auto first = parent.children.size();
        fn();
        for (auto i = first; i < parent.children.size(); ++i) {
            auto& d = parent.children[i].drag;
            if (role.source) {
                d.source = role.source;
                d.ghost_area = role.ghost_area;
                d.outside = role.outside;
                d.trail = role.trail;
                d.hint_targets = role.hint_targets;
            }
            if (role.accepts) {
                d.accepts = role.accepts;
                d.drop = role.drop;
                d.between = role.between;
                d.inside_middle = role.inside_middle;
                d.zones = role.zones;
                d.edge_zones = role.edge_zones;
                d.claims = role.claims;
                d.takes_at = role.takes_at;
                d.slot = role.slot;
                d.slot_marker = role.slot_marker;
            }
        }
    }

    // A panel of a dock: its name (what the layout stores), its tab title, and what it shows.
    // `closable`: its tab has a close button (ui::show_panel opens it again: give the user a way,
    // a View menu, before making panels closable).
    struct DockPanel {
        std::string name;
        std::string title;
        std::function<void(ViewBuilder&)> build;
        bool closable = false;
    };

    // A dock (doc/design/drag-and-docking.md): the panels arranged as `layout` says, in splits
    // and tab groups. Defined in app/dock_builder.hpp.
    void dock(Field<DockLayout>& layout, std::vector<DockPanel> panels);

public:
    // A right click anywhere in the current container opens this menu at the pointer.
    void context_menu(Field<ContextMenu>& menu) { widget(menu); }

    // A modal dialog, shown when the app sets its `open` (see ui::Dialog): its title, its message
    // (selectable, scrolling when long), the widgets `content` places (a form; none by default)
    // and a row of its buttons. The first button has the focus, so Enter picks it.
    void dialog(Field<Dialog>& d) { dialog(d, [](ViewBuilder&) {}); }

    template <typename F>
    void dialog(Field<Dialog>& d, F&& content) {
        widget(d);
        auto parts = std::make_shared<view_builder_detail::DialogParts>();
        attach_popup_content([&tree = tree_, &d, parts, cb = std::forward<F>(content)](ViewBuilder& vb) mutable {
            parts->sync(d, tree.theme().measurer);
            vb.widget(parts->title);
            if (!d.get().message.empty()) vb.widget(parts->message);
            cb(vb);
            vb.hstack([&] {
                vb.spacer();
                for (std::size_t i = 0; i < d.get().buttons.size(); ++i) {
                    if (i > 0) vb.gap(8.f);
                    auto b = vb.widget(*parts->buttons[i]);
                    if (i == 0) std::move(b).focus_on_open();
                }
            });
        });
    }

    // A popup holding the widgets `content` places (see ui::Popup and
    // doc/design/popup-content.md). They are built when the popup opens and dropped when it
    // closes, so `content` runs again on each opening.
    template <typename F>
    void popup(Field<Popup>& state, F&& content) {
        widget(state);
        attach_popup_content(std::forward<F>(content));
    }

    // Renders like widget(Field<T>&) but without focus/input wiring --
    // for prism::inspector's [[=prism::inspector::readonly]] annotation.
    template <typename T>
    LeafHandle widget_readonly(Field<T>& field) {
        placed_.insert(&field);
        auto leaf = node_readonly_leaf<T>(field, tree_.next_id_);
        apply_field_name(leaf, field);
        current_parent().children.push_back(std::move(leaf));
        return {current_parent().children.back(), placed_};
    }

    template <typename T>
    LeafHandle widget(Derived<T>& derived) {
        placed_.insert(&derived);
        auto leaf = node_readonly_leaf<T>(derived, tree_.next_id_);
        apply_field_name(leaf, derived);
        current_parent().children.push_back(std::move(leaf));
        return {current_parent().children.back(), placed_};
    }

    template <typename T>
    LeafHandle widget(Shared<T>& shared) {
        placed_.insert(&shared);
        auto node = node_readonly_leaf<T>(shared, tree_.next_id_);
        apply_field_name(node, shared);
        node.drain_fn = [&shared] { shared.drain_notifications(); };
        node.bind_waker_fn = [&shared](std::function<void()> w) { shared.set_waker(std::move(w)); };
        current_parent().children.push_back(std::move(node));
        return {current_parent().children.back(), placed_};
    }

    // Generic read-only widget for custom observables (e.g. Python Derived).
    template <typename T, typename Obs>
    void widget_generic(Obs& obs) {
        placed_.insert(static_cast<const void*>(&obs));
        auto leaf = node_readonly_leaf<T>(obs, tree_.next_id_);
        apply_field_name(leaf, obs);
        current_parent().children.push_back(std::move(leaf));
    }

    [[nodiscard]] const std::set<const void*>& placed() const { return placed_; }

    template <typename C>
    void block(C& comp) {
        auto& parent = current_parent();
        parent.children.push_back(tree_.build_node_tree(comp));
        auto& root = parent.children.back();
        root.block = std::string(view_builder_detail::block_name<C>());
        // In a toolbar or status bar the block's widgets are the bar's items: they run along
        // the bar's row, and a spacer among them stretches across it, not down the window.
        if (view_builder_detail::is_bar(parent)) root.layout_kind = parent.layout_kind;
    }

    void vstack(auto&... args) { push_container(LayoutKind::Column, [&] { (item(args), ...); }); }

    WidgetId hstack(std::invocable auto&& fn) { return push_container(LayoutKind::Row, fn).id; }
    void hstack(auto&... args) { push_container(LayoutKind::Row, [&] { (item(args), ...); }); }
    WidgetId vstack(std::invocable auto&& fn) { return push_container(LayoutKind::Column, fn).id; }

    // Children over one another: the first fills the area; each later one lies where `places()`
    // says (in the area's coordinates, kept inside it), drawn over the first and hit before it.
    // `places` is read at every layout, so moving one needs no rebuild.
    WidgetId layers(std::function<std::vector<Rect>()> places, std::invocable auto&& fn) {
        auto& n = push_container(LayoutKind::Layers, fn);
        n.layer_places = std::move(places);
        return n.id;
    }
    // A node editor (prism/nodes/graph.hpp): each node a card at its position (a header with its
    // title and family, then its view()), the wires drawn between the cards' port rows. Built
    // again when a node comes or goes; moving one or adding a wire rebuilds no widget.
    WidgetId node_board(nodes::Graph& graph);
    // A port's row in a node's view: its name, on the left for an input, on the right for an
    // output; the port's wires end at the card's edge level with it. Only inside a node_board.
    LeafHandle port(nodes::Port& p);

    // A raised surface on its own background, outlined: what lies over other widgets (a card
    // in vb.layers).
    WidgetId card(std::invocable auto&& fn) { return framed(LayoutKind::Column, {FrameLook::Card}, fn); }

    // A titled section the user folds and unfolds: `state`'s header, then the content `build`
    // places (as a tab's builder does), built only while the section is open -- folded-away
    // widgets take no focus and no clicks.
    void section(Field<Expander>& state, std::function<void(ViewBuilder&)> build);

    // A row of tools across the window (buttons, toggles), on the window's chrome colour.
    WidgetId toolbar(std::invocable auto&& fn) { return framed(LayoutKind::Row, {FrameLook::Toolbar}, fn); }
    // A row of short status texts, usually last in the window.
    WidgetId status_bar(std::invocable auto&& fn) { return framed(LayoutKind::Row, {FrameLook::StatusBar}, fn); }
    // Widgets that belong together, outlined, under a title.
    WidgetId group(std::string title, std::invocable auto&& fn) {
        return framed(LayoutKind::Column, {FrameLook::Group, std::move(title)}, fn);
    }
    // The general form of the four above: a container with a look of its own (a node board's
    // card and header are made of it too).
    WidgetId framed(LayoutKind kind, Framing frame, std::invocable auto&& fn) {
        return push_container(kind, fn, std::move(frame)).id;
    }

private:
    // Gives the widget just placed a popup built from `content` on each opening. It also runs
    // once now, into a scratch node, only to learn which fields it places, so the unplaced-field
    // check doesn't flag them.
    void add_spacer(float size);

    template <typename F>
    void attach_popup_content(F&& content) {
        Node scratch;
        ViewBuilder probe{tree_, scratch};
        content(probe);
        placed_.insert(probe.placed().begin(), probe.placed().end());
        current_parent().children.back().popup_builder =
            [&tree = tree_, cb = std::forward<F>(content)](Node& target) mutable {
                ViewBuilder vb{tree, target};
                cb(vb);
            };
    }

    void item(field_type auto& field) { widget(field); }
    template <typename T>
    void item(Derived<T>& d) { widget(d); }
    template <typename T>
    void item(Shared<T>& s) { widget(s); }
    void item(block_type auto& comp) { block(comp); }
    // Runs `build` once into a scratch node, only to learn which fields it places: content built
    // later (a dock's panels, a section's) counts as placed by the view.
    void probe_placed(const std::function<void(ViewBuilder&)>& build);

    // The frame is set before `fn` builds the content: a block placed in a bar reads it.
    Node& push_container(LayoutKind kind, std::invocable auto&& fn, std::optional<Framing> frame = {}) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = kind;
        container.frame = std::move(frame);
        auto& parent = current_parent();
        parent.children.push_back(std::move(container));
        auto& ref = parent.children.back();
        stack_.push_back(&ref);
        fn();
        stack_.pop_back();
        if (kind == LayoutKind::Row || kind == LayoutKind::Column)
            wire_split_handles(ref);
        return ref;
    }

    // Shared by handle() (a bare divider with no drag wiring, e.g. if placed
    // outside a Row/Column) and wire_split_handles() (the real, draggable one) --
    // the hit-rect Handle gets in layout_flatten is the bounding box of whatever
    // this paints, so it must always cover the node's full allocated size.
    static void draw_divider(WidgetNode& node);

    void wire_split_handles(Node& container);

public:

    void spacer() { add_spacer(0.f); }

    // A fixed gap of `points` along the current row or column (between buttons, around groups).
    void gap(float points) { add_spacer(points); }

    void handle();

    void scroll(std::invocable auto&& fn) {
        auto& node = push_container(LayoutKind::Scroll, fn);
        node.scroll_bar_policy = ScrollBarPolicy::Auto;
        node.scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
    }

    void scroll(ScrollBarPolicy policy, std::invocable auto&& fn) {
        auto& node = push_container(LayoutKind::Scroll, fn);
        node.scroll_bar_policy = policy;
        node.scroll_event_policy = ScrollEventPolicy::BubbleAtBounds;
    }

    void scroll(Field<ScrollArea>& field, std::invocable auto&& fn) {
        placed_.insert(&field);
        auto& scroll_node = push_container(LayoutKind::Scroll, fn);
        scroll_node.scroll_bar_policy = field.get().scrollbar;
        scroll_node.scroll_event_policy = field.get().event_policy;
        scroll_node.build_widget = [&field](WidgetNode& wn) {
            auto& ss = wn.get_or_create<ScrollState>();
            ss.scrollbar = field.get().scrollbar;
            ss.event_policy = field.get().event_policy;
            ss.offset_y = field.get().scroll_y;
        };
        scroll_node.on_change = view_builder_detail::adapt_on_change(field);
    }

    // A document, shown: only the blocks in sight are laid out, so a long one scrolls as fast as a
    // short one (text/view.hpp). It redraws when the document changes.
    TextHandle text_view(text::Document& doc, text::ViewOptions options = {});
    // The same, edited: a caret, a selection, the keys and the mouse of a text editor
    // (text/editor.hpp). Undo is the document's. Focused by a click or Tab.
    TextHandle text_editor(text::Document& doc, text::EditorOptions options = {});

    // A code editor: mono, long lines scrolled sideways (not wrapped), line numbers, the caret's
    // line shaded, `language`'s colors
    // (text::language: "cpp", "python", "sql", "json", "toml", "shell"...), Tab to indent, Enter
    // keeping the indent, brackets matched, and completing the file's words and the language's
// keywords. text_editor with options for anything else.
    TextHandle code_editor(text::Document& doc, std::string_view language = {});

    // Code shown, not edited: `language`'s colors, selectable and copyable, no gutter or minimap
    // (a snippet in a page). .rows(n) for its height in a scroll area.
    TextHandle code_view(text::Document& doc, std::string_view language = {});

    template <typename T>
        requires requires(T& t, DrawList& dl, Rect r, const WidgetNode& n) {
            t.canvas(dl, r, n);
        }
    auto canvas(T& model) {
        current_parent().children.push_back(node_canvas(model, tree_.next_id_));
        return CanvasHandle{current_parent().children.back(), placed_};
    }

    // What vb.list returns: the list just placed, to set options on.
    template <typename T>
    struct ListHandle {
        Node& node_ref;
        List<T>& items;
        WidgetTree& tree;
        std::set<const void*>& placed;
        // Rows appended while the list shows its last row scroll it to the new last row, as a
        // terminal does; scrolling up stops that, scrolling back to the bottom resumes it.
        ListHandle&& follow_tail() && {
            node_ref.vlist_follow_tail = true;
            return std::move(*this);
        }
        // The user can drag rows to put them in another order (List::move).
        ListHandle&& reorderable() && {
            node_ref.row_drag = [&items = items](std::size_t i) { return view_builder_detail::list_row_drag(items, i); };
            return std::move(*this);
        }
        // `selected` holds the selected row: a click selects it, the row is highlighted, and a
        // removal keeps it on the same item (none, once that item goes).
        // The list takes the focus: Up/Down/Home/End move the selection (a row whose own widget
        // takes the focus, an editor, keeps the keys).
        ListHandle&& selection(Field<std::optional<std::size_t>>& selected) && {
            placed.insert(&selected);
            // simplify: each placement follows the list on its own, so two lists sharing one
            // selection field shift it twice; share one follower per field if that comes up.
            auto follow = std::make_shared<std::array<Connection, 3>>(std::array{
                items.on_insert().connect([&selected](std::size_t at, const auto&) {
                    if (auto s = selected.get(); s && at <= *s) selected.set(*s + 1);
                }),
                items.on_remove().connect([&selected](std::size_t gone) {
                    auto s = selected.get();
                    if (!s || *s < gone) return;
                    selected.set(*s == gone ? std::nullopt : std::optional{*s - 1});
                }),
                items.on_move().connect([&selected](std::size_t from, std::size_t to) {
                    if (auto s = selected.get()) selected.set(index_after_move(*s, from, to));
                })});
            node_ref.vlist_bind_row = [bind = std::move(node_ref.vlist_bind_row), &selected, follow, &tree = tree,
                                       list_id = node_ref.id](WidgetNode& wn, size_t index) {
                bind(wn, index);
                view_builder_detail::show_selection(wn, index, selected, tree, list_id);
            };
            node_ref.build_widget = [&items = items, &selected, &tree = tree](WidgetNode& wn) {
                view_builder_detail::select_by_keys(wn, items, selected, tree);
            };
            return std::move(*this);
        }
    };

    template <typename T>
    ListHandle<T> list(List<T>& items, std::function<void(size_t, const T&)> on_row_click = nullptr) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::VirtualList;
        container.vlist_item_count = items.size();

        auto shifted_at = std::make_shared<std::uint64_t>(0);
        container.vlist_bind_row = [&items, on_row_click, shifted_at, &tree = tree_](WidgetNode& wn, size_t index) {
            auto field_ptr = std::make_shared<Field<T>>(items[index]);
            wn.edit_state = std::shared_ptr<void>(field_ptr);
            wn.focus_policy = Widget<T>::focus_policy;
            wn.dirty = true;
            wn.is_container = false;
            wn.draws.clear();
            wn.overlay_draws.clear();
            wn.record = [field_ptr](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                Widget<T>::record(node.draws, *field_ptr, node);
            };
            wn.record(wn);
            wn.wire = [field_ptr, &items, index, on_row_click, shifted_at, &tree](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([field_ptr, &node](const InputEvent& ev) {
                        Widget<T>::handle_input(*field_ptr, ev, node);
                    })
                );
                node.connections.push_back(
                    field_ptr->on_change().connect([field_ptr, &items, index](const T&) {
                        if (index < items.size()) items.set(index, field_ptr->get());
                    })
                );
                if (on_row_click) {
                    // Fires synchronously with a live items[index] ref during dispatch --
                    // a handler that mutates the list (e.g. erasing this row) risks it
                    // dangling mid-call; capture index/copy rather than mutate through it.
                    // Rows are recycled widgets bound to an index. A click hit-tested against
                    // a frame shown before a removal moved the rows (see WindowEvent::shown)
                    // would report the row's neighbour: it is dropped instead, since without
                    // item keys the item the user saw cannot be found again. Appends and
                    // in-place updates move nothing and never drop a click.
                    node.connections.push_back(
                        node.on_input.connect([on_row_click, &items, index, shifted_at, &tree, &node](const InputEvent& ev) {
                            if (tree.input_frame() < *shifted_at) return;
                            // a click: on release, over the row pressed (a drag is not one)
                            if (delegate_detail::is_activation_event(ev, node) && std::holds_alternative<MouseButton>(ev)
                                && index < items.size())
                                on_row_click(index, items[index]);
                        })
                    );
                }
            };
        };

        container.vlist_unbind_row = &view_builder_detail::unbind_vlist_row;
        view_builder_detail::wire_list_signals(container, items);
        // A removal moves the rows after it: remember the first frame that shows them moved.
        container.vlist_on_remove = [inner = container.vlist_on_remove, shifted_at, &tree = tree_](
                                        size_t i, std::function<void()> cb) {
            return inner(i, [cb = std::move(cb), shifted_at, &tree] {
                *shifted_at = tree.next_frame_version();
                cb();
            });
        };

        current_parent().children.push_back(std::move(container));
        return {current_parent().children.back(), items, tree_, placed_};
    }

    // A tree, with a pane beside it showing the selected row's details; `detail = false` places
    // the tree alone (the app shows the selection its own way, through ctrl.selected).
    void tree(TreeController& ctrl, bool detail = true);

    template <ColumnStorage T>
    TableBuilder table(T& data) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::Table;

        auto state = std::make_shared<TableState>();
        state->source = wrap_column_storage(data);
        state->column_count = data.column_count();
        container.table_state = state;

        current_parent().children.push_back(std::move(container));
        return TableBuilder{current_parent().children.back(), placed_};
    }

#if __cpp_impl_reflection
    template <typename T>
        requires RowStorage<List<T>>
    TableBuilder table(List<T>& list) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::Table;

        auto state = std::make_shared<TableState>();
        state->source = wrap_row_storage(list);
        state->column_count = state->source.column_count();
        view_builder_detail::track_rows(*state, list);
        container.table_state = state;

        view_builder_detail::wire_list_signals(container, list);

        current_parent().children.push_back(std::move(container));
        return TableBuilder{current_parent().children.back(), placed_};
    }

    template <typename T>
        requires SoaStorage<T>
    TableBuilder table(T& data) {
        Node container;
        container.id = tree_.next_id_++;
        container.is_leaf = false;
        container.layout_kind = LayoutKind::Table;

        auto state = std::make_shared<TableState>();
        state->source = wrap_soa_columns(data);
        state->column_count = state->source.column_count();
        container.table_state = state;

        current_parent().children.push_back(std::move(container));
        return TableBuilder{current_parent().children.back(), placed_};
    }
#endif // __cpp_impl_reflection

    // Tabs: a header to pick a page, and the page; only the selected page is built.
    void tabs(Field<TabBar<>>& field, std::invocable auto&& builder) { switched_pages(field, builder, true, TabActions{}); }
    // What a tab bar's tabs do beyond picking a page: dragged, taking drops, closed.
    struct TabActions {
        std::function<std::any(std::size_t, Point)> source{};   // what dragging tab i carries (pressed at, in the bar)
        std::function<void(const std::any&)> outside{};       // a dragged tab let go outside the window
        std::function<bool(const std::any&)> accepts{};       // the bar takes this value between its tabs?
        std::function<void(const std::any&, std::size_t)> drop{};   // ... dropped before tab i (count: after the last)
        std::function<bool(std::size_t)> closable{};          // tab i has a close button (none: all, with close)
        std::function<void(std::size_t)> close{};             // its button, or a middle click on it
        std::function<void(std::size_t)> detach{};            // a double click, or the bar's button (shown tab)
    };
    void tabs(Field<TabBar<>>& field, std::invocable auto&& builder, TabActions drag) {
        switched_pages(field, builder, true, std::move(drag));
    }

    // Tabs over a list, one per item and kept in step with it: `title(item)` names a tab,
    // `page(vb, item)` builds the shown one's page. .closable() and .reorderable() change the list.
    struct ListTabsOptions {
        std::function<bool(std::size_t)> closable{};
        bool reorderable = false;
    };
    struct ListTabsHandle {
        std::shared_ptr<ListTabsOptions> options;
        // A close button on each tab (or on those `can_close(i)` allows), and a middle click: the
        // item leaves the list.
        ListTabsHandle&& closable(std::function<bool(std::size_t)> can_close = {}) && {
            options->closable = can_close ? std::move(can_close) : [](std::size_t) { return true; };
            return std::move(*this);
        }
        // A tab dragged along the bar moves its item there (List::move), and is shown.
        ListTabsHandle&& reorderable() && {
            options->reorderable = true;
            return std::move(*this);
        }
    };
    template <typename T>
    ListTabsHandle tabs(Field<TabBar<>>& selected, List<T>& items,
                        std::type_identity_t<std::function<std::string(const T&)>> title,
                        std::type_identity_t<std::function<void(ViewBuilder&, const T&)>> page) {
        placed_.insert(&selected);
        for (const auto& item : items) probe_placed([&](ViewBuilder& p) { page(p, item); });
        auto options = std::make_shared<ListTabsOptions>();
        auto version = std::make_shared<std::uint64_t>(0);
        auto built = std::make_shared<std::uint64_t>(0);
        auto follow = view_builder_detail::follow_list_tabs(items, selected, version);
        Node c;
        c.id = tree_.next_id_++;
        c.is_leaf = false;
        c.layout_kind = LayoutKind::Column;
        c.dependencies = view_builder_detail::list_changes(items);
        c.content_stale = [version, built] { return *version != *built; };
        c.build_content = [&tree = tree_, &selected, &items, title, page, options, version, built, follow](Node& target) {
            *built = *version;
            ViewBuilder vb{tree, target};
            vb.tabs(selected, [&] {
                for (std::size_t i = 0; i < items.size(); ++i)
                    vb.tab(title(items[i]), [&items, i, page](ViewBuilder& p) { page(p, items[i]); });
            }, list_tab_actions(items, selected, *options));
        };
        current_parent().children.push_back(std::move(c));
        return {options};
    }

private:
    template <typename T>
    static TabActions list_tab_actions(List<T>& items, Field<TabBar<>>& selected, const ListTabsOptions& options) {
        TabActions a;
        if (options.reorderable) {
            a.source = [&items](std::size_t i, Point) { return std::any(view_builder_detail::ListTabDrag{&items, i}); };
            a.accepts = [&items](const std::any& v) {
                auto* d = std::any_cast<view_builder_detail::ListTabDrag>(&v);
                return d && d->list == &items;
            };
            a.drop = [&items, &selected](const std::any& v, std::size_t before) {
                const std::size_t from = std::any_cast<const view_builder_detail::ListTabDrag&>(v).index;
                const std::size_t to = std::min(before > from ? before - 1 : before, items.size() - 1);
                items.move(from, to);
                selected.set({.selected = to});
            };
        }
        if (options.closable) {
            // The bar may be drawn once more before it follows a removal: never ask past the end.
            a.closable = [&items, can = options.closable](std::size_t i) { return i < items.size() && can(i); };
            a.close = [&items](std::size_t i) { if (i < items.size()) items.erase(i); };
        }
        return a;
    }

public:
    // Pages: tabs without the header, the app picks the page through `field.selected` (a
    // detail view that follows a selection, a wizard's steps). Pages are declared with tab().
    void pages(Field<TabBar<>>& field, std::invocable auto&& builder) { switched_pages(field, builder, false, TabActions{}); }

private:
    void switched_pages(Field<TabBar<>>& field, std::invocable auto&& builder, bool header, TabActions drag) {
        placed_.insert(&field);

        auto state = std::make_shared<TabsState>();
        state->get_selected = [&field]() { return field.get().selected; };

        auto* prev_tabs_state = current_tabs_state_;
        current_tabs_state_ = state.get();
        builder();
        current_tabs_state_ = prev_tabs_state;

        Node tabs_node;
        tabs_node.id = tree_.next_id_++;
        tabs_node.is_leaf = false;
        tabs_node.layout_kind = LayoutKind::Tabs;
        tabs_node.tabs_state = state;

        // Child 0: the tab bar, or for pages an empty spacer (the layout gives it no height).
        if (header) {
            auto bar = node_leaf(field, tree_.next_id_);
            auto bar_build = bar.build_widget;
            auto names_ptr = state->tab_names;
            bar.build_widget = [bar_build, names_ptr, closable = std::move(drag.closable),
                                close = std::move(drag.close), detach = std::move(drag.detach)](WidgetNode& wn) {
                wn.tab_names = names_ptr;
                if (detach) wn.get_or_create<TabBarEditState>().detach = detach;
                if (close) {
                    auto& es = wn.get_or_create<TabBarEditState>();
                    es.close = close;
                    es.closable = closable ? closable : [](std::size_t) { return true; };
                }
                if (bar_build) bar_build(wn);
            };
            if (drag.accepts) {   // a value dropped between two tabs
                bar.drag.accepts = std::move(drag.accepts);
                bar.drag.drop = [drop = std::move(drag.drop)](const std::any& v, DropPlace at) { drop(v, at.slot); };
                bar.drag.slot = [](const WidgetNode& n, Point at) { return tabs_detail::insertion_at(n, at.x); };
                bar.drag.slot_marker = tabs_detail::insertion_marker;
            }
            if (drag.source)   // a tab pressed and moved drags what drag.source gives for it
            {
                bar.drag.source = [tab_drag = std::move(drag.source)](const WidgetNode& n, Point at) {
                    auto i = tabs_detail::tab_at(n, at.x);
                    return i ? tab_drag(*i, at) : std::any{};
                };
                bar.drag.outside = std::move(drag.outside);
                bar.drag.ghost_area = [](const WidgetNode& n, Point at) -> std::optional<Rect> {   // that tab only
                    auto i = tabs_detail::tab_at(n, at.x);
                    if (!i) return std::nullopt;
                    auto [x0, x1] = n.peek<TabBarEditState>().header_x_ranges[*i];
                    return Rect{Point{x0, Y{0}}, Size{Width{x1.raw() - x0.raw()}, delegate_detail::widget_h(n)}};
                };
            }
            tabs_node.children.push_back(std::move(bar));
        } else {
            Node none;
            none.id = tree_.next_id_++;
            none.is_leaf = true;
            none.layout_kind = LayoutKind::Spacer;
            none.on_change = view_builder_detail::adapt_on_change(field);   // a new pick redraws
            tabs_node.children.push_back(std::move(none));
        }

        // Content container (child 1) — filled by materialize_tabs on first frame
        Node content;
        content.id = tree_.next_id_++;
        content.is_leaf = false;
        content.layout_kind = LayoutKind::Column;
        tabs_node.children.push_back(std::move(content));

        current_parent().children.push_back(std::move(tabs_node));
    }

public:
    template <typename F>
        requires std::invocable<F, ViewBuilder&>
    void tab(std::string_view name, F&& content_builder) {
        if (!current_tabs_state_) return;
        probe_placed(content_builder);   // pages are built when shown: learn what they place now
        if (!current_tabs_state_->tab_names)
            current_tabs_state_->tab_names = std::make_shared<std::vector<std::string>>();
        current_tabs_state_->tab_names->push_back(std::string(name));
        current_tabs_state_->tab_node_builders.push_back(
            [&tree = tree_, cb = std::forward<F>(content_builder)]
            (Node& target) mutable {
                ViewBuilder vb{tree, target};
                cb(vb);
            });
    }

#if __cpp_impl_reflection
    template <typename S>
        requires (!std::is_void_v<S>)
    void tabs(Field<TabBar<S>>& field) {
        placed_.insert(&field);

        auto state = std::make_shared<TabsState>();
        state->tab_names = std::make_shared<std::vector<std::string>>();
        state->get_selected = [&field]() { return field.get().selected; };

        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(
                ^^S, std::meta::access_context::unchecked()));

        template for (constexpr auto m : members) {
            state->tab_names->push_back(
                std::string(std::meta::identifier_of(m)));

            auto& member = field.value.pages.[:m:];
            state->tab_node_builders.push_back(
                [&member, &tree = tree_](Node& target) {
                    target.children.push_back(tree.build_node_tree(member));
                });
        }

        Node tabs_node;
        tabs_node.id = tree_.next_id_++;
        tabs_node.is_leaf = false;
        tabs_node.layout_kind = LayoutKind::Tabs;
        tabs_node.tabs_state = state;

        // Tab bar leaf — custom build since we don't have a Field<TabBar<>>
        Node bar;
        bar.id = tree_.next_id_++;
        bar.is_leaf = true;
        auto names_ptr = state->tab_names;
        bar.build_widget = [&field, names_ptr](WidgetNode& wn) {
            wn.focus_policy = FocusPolicy::tab_and_click;
            wn.tab_names = names_ptr;
            wn.record = [&field, names_ptr](WidgetNode& node) {
                node.draws.clear();
                node.overlay_draws.clear();
                prism::ui::tabs_detail::tabs_record(node.draws, node, field.get().selected, *names_ptr);
            };
            wn.record(wn);
            wn.wire = [&field, names_ptr](WidgetNode& node) {
                node.connections.push_back(
                    node.on_input.connect([&field, names_ptr, &node](const InputEvent& ev) {
                        size_t count = names_ptr->size();
                        if (count == 0) return;
                        prism::ui::tabs_detail::tabs_handle_input(ev, node, field.get().selected, count,
                            [&field](size_t idx) {
                                field.value.selected = idx;
                                field.on_change().emit(field.value);
                            });
                    })
                );
            };
        };
        bar.on_change = view_builder_detail::adapt_on_change(field);
        tabs_node.children.push_back(std::move(bar));

        // Content container
        Node content;
        content.id = tree_.next_id_++;
        content.is_leaf = false;
        content.layout_kind = LayoutKind::Column;
        tabs_node.children.push_back(std::move(content));

        current_parent().children.push_back(std::move(tabs_node));
    }
#endif

    void finalize();
};

} // namespace prism::app

#include <prism/app/dock_builder.hpp>
