#pragma once

#include <prism/core/field.hpp>
#include <prism/core/list.hpp>
#include <prism/core/reflect_leaf.hpp>
#include <prism/core/types.hpp>
#include <prism/delegates/text_delegates.hpp>
#include <prism/input/input_event.hpp>
#include <prism/ui/delegate.hpp>

#include <cstdint>
#include <fmt/format.h>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#endif

namespace prism::ui {
using namespace prism::core;

using TreeNodeId = uint64_t;

struct TreeSource {
    std::function<size_t()> root_count;
    std::function<TreeNodeId(size_t)> root_at;
    std::function<size_t(TreeNodeId)> child_count;
    std::function<TreeNodeId(TreeNodeId, size_t)> child_at;
    std::function<std::string(TreeNodeId)> label;
    std::function<bool(TreeNodeId)> has_children;
    std::function<std::optional<std::string>(TreeNodeId)> icon{};   // a glyph from the icon font
    // An image icon (render::load_image reads one from a file); it wins over the glyph. Images
    // draw the same everywhere, while the web build ships only a few of the font's glyphs.
    std::function<std::shared_ptr<const Image>(TreeNodeId)> image{};
    std::function<std::vector<std::pair<std::string, std::string>>(TreeNodeId)> attributes{};
    // A checkbox on the node (nullopt: none), and what a click on it does.
    // simplify: two states only; a parent showing "some children ticked" needs a third.
    std::function<std::optional<bool>(TreeNodeId)> checked{};
    std::function<void(TreeNodeId, bool)> set_checked{};
    // Renames the node in place (F2, double-click); false refuses the name and keeps the editor.
    std::function<bool(TreeNodeId, std::string_view)> rename{};
};

template <typename T>
concept TreeStorage = requires(const T& t, TreeNodeId id, size_t i) {
    { t.root_count() } -> std::convertible_to<size_t>;
    { t.root_at(i) } -> std::convertible_to<TreeNodeId>;
    { t.child_count(id) } -> std::convertible_to<size_t>;
    { t.child_at(id, i) } -> std::convertible_to<TreeNodeId>;
    { t.label(id) } -> std::convertible_to<std::string>;
    { t.has_children(id) } -> std::convertible_to<bool>;
};

template <TreeStorage T>
TreeSource wrap_tree_storage(T& data) {
    TreeSource src;
    src.root_count = [&data] { return data.root_count(); };
    src.root_at = [&data](size_t i) { return data.root_at(i); };
    src.child_count = [&data](TreeNodeId id) { return data.child_count(id); };
    src.child_at = [&data](TreeNodeId id, size_t i) { return data.child_at(id, i); };
    src.label = [&data](TreeNodeId id) { return data.label(id); };
    src.has_children = [&data](TreeNodeId id) { return data.has_children(id); };
    if constexpr (requires(const T& t, TreeNodeId id) { t.icon(id); }) {
        src.icon = [&data](TreeNodeId id) { return data.icon(id); };
    }
    if constexpr (requires(const T& t, TreeNodeId id) { t.image(id); }) {
        src.image = [&data](TreeNodeId id) { return data.image(id); };
    }
    if constexpr (requires(const T& t, TreeNodeId id) { t.attributes(id); }) {
        src.attributes = [&data](TreeNodeId id) { return data.attributes(id); };
    }
    if constexpr (requires(T& t, TreeNodeId id) { t.checked(id); t.set_checked(id, true); }) {
        src.checked = [&data](TreeNodeId id) { return data.checked(id); };
        src.set_checked = [&data](TreeNodeId id, bool on) { data.set_checked(id, on); };
    }
    if constexpr (requires(T& t, TreeNodeId id, std::string_view s) { { t.rename(id, s) } -> std::convertible_to<bool>; }) {
        src.rename = [&data](TreeNodeId id, std::string_view s) { return data.rename(id, s); };
    }
    return src;
}

// A node's name being edited in place.
struct TreeRename {
    TextEdit text{};
    bool rejected = false;   // the source refused the last commit
    bool operator==(const TreeRename&) const = default;
};

struct TreeRow {
    TreeNodeId id = 0;
    std::string label;
    std::optional<std::string> icon;
    int depth = 0;
    bool has_children = false;
    bool expanded = false;
    bool selected = false;
    std::shared_ptr<const Image> image{};   // drawn in the icon slot instead of `icon`
    std::optional<bool> checked{};          // the node's checkbox, if it has one
    std::optional<TreeRename> rename{};     // the editor over the label, while renaming

    bool operator==(const TreeRow&) const = default;
};

struct TreeDetail {
    std::string label;
    std::vector<std::pair<std::string, std::string>> attributes;

    bool operator==(const TreeDetail&) const = default;
};

template <>
struct Widget<TreeRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Role access_role = Role::TreeItem;
    static void describe(const Field<TreeRow>& f, AccessInfo& a) {
        const auto& r = f.get();
        a.name = r.label;
        a.selected = r.selected;
        a.checked = r.checked;
        if (r.has_children) a.expanded = r.expanded;
    }
    static constexpr Height row_h{22.f};
    static constexpr float font = 13.f, marker_slot = 14.f, check_slot = 20.f, box = 16.f;

    // Where a row's checkbox starts, and whether a click at `x` (row-local) lands on it.
    static float check_x(const TreeRow& row) { return 8.f + static_cast<float>(row.depth) * 16.f + marker_slot; }
    static bool on_check(const TreeRow& row, float x) {
        return row.checked && x >= check_x(row) && x < check_x(row) + box;
    }

    static void record(DrawList& dl, const Field<TreeRow>& field, WidgetNode& node) {
        auto& row = field.get();
        auto& vs = node_vs(node);
        auto& t = node_theme(node);
        bool highlight = vs.hovered || row.selected;
        auto bg = highlight ? t.surface_hover : t.surface;
        auto w = delegate_detail::widget_w(node);
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, row_h), bg);

        // Marker, icon and label are separate commands in fixed slots: icon glyphs (Nerd Font,
        // via the mono fallback) ink wider than their advance, so in one string they ran into
        // the label once the UI font's space was narrower than the mono font's.
        constexpr float icon_slot = 1.6f * font;
        X x = X{8.f} + DX{static_cast<float>(row.depth) * 16.f};
        if (row.has_children)
            dl.text(row.expanded ? "v" : ">", delegate_detail::make_point(x, Y{4.f}), font, t.text);
        x = x + DX{marker_slot};
        if (row.checked) {
            draw_check_box(dl, x, Y{(row_h.raw() - box) / 2}, *row.checked, WidgetVisualState{}, t);
            x = x + DX{check_slot};
        }
        if (row.image) {
            constexpr float image_size = 16.f;
            dl.image(row.image, Rect{delegate_detail::make_point(x, Y{(row_h.raw() - image_size) / 2}),
                                     Size{Width{image_size}, Height{image_size}}},
                     icon_tint(row.image, t.text));
            x = x + DX{icon_slot};
        } else if (row.icon) {
            dl.text(*row.icon, delegate_detail::make_point(x, Y{4.f}), font, t.text);
            x = x + DX{icon_slot};
        }
        if (row.rename)
            draw_line_editor(dl, row.rename->text, row.rename->rejected,
                             Rect{delegate_detail::make_point(x - DX{4.f}, Y{1.f}), Size{Width{w.raw() - x.raw() + 2.f}, Height{row_h.raw() - 2.f}}},
                             font, t);
        else
            dl.text(row.label, delegate_detail::make_point(x, Y{4.f}), font, t.text);
    }

    // Selection/expand-toggle/keyboard nav are handled centrally by TreeController via the
    // container's row-click and container-level keyboard wiring (see ViewBuilder::tree(),
    // Task 5/6) -- an individual row has nothing of its own to mutate on input.
    static void handle_input(Field<TreeRow>&, const InputEvent&, WidgetNode&) {}
};

template <>
struct Widget<std::optional<TreeDetail>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    // Fallback only, used before layout has ever run (node_allocated() still zero) --
    // vb.tree() places this widget in an hstack alongside the row list, so its real
    // allocated height is the full viewport height (the hstack's cross axis), not a fixed
    // size. Drawing to a hardcoded height regardless of allocation left the rest of the
    // column unpainted -- showing raw canvas background as the list (and its window) grew
    // taller.
    static constexpr Height panel_h{200.f};

    static void record(DrawList& dl, const Field<std::optional<TreeDetail>>& field, WidgetNode& node) {
        auto& t = node_theme(node);
        auto w = delegate_detail::widget_w(node);
        auto allocated_h = node_allocated(node).h;
        auto h = allocated_h.raw() > 0.f ? allocated_h : panel_h;
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, h), t.surface);

        auto& value = field.get();
        if (!value) {
            dl.text("No selection", delegate_detail::make_point(X{8.f}, Y{8.f}), 13, t.text);
            return;
        }
        Y y{8.f};
        dl.text(value->label, delegate_detail::make_point(X{8.f}, y), 13, t.text);
        y = y + DY{18.f};
        for (auto& [k, v] : value->attributes) {
            dl.text(k + ": " + v, delegate_detail::make_point(X{8.f}, y), 13, t.text);
            y = y + DY{18.f};
        }
    }

    static void handle_input(Field<std::optional<TreeDetail>>&, const InputEvent&, WidgetNode&) {}
};

namespace tree_detail {
inline void flatten_node(const TreeSource& source, TreeNodeId id, int depth,
                          const std::set<TreeNodeId>& expanded,
                          const std::set<TreeNodeId>& selected,
                          std::vector<TreeRow>& out) {
    TreeRow row;
    row.id = id;
    row.label = source.label(id);
    row.icon = source.icon ? source.icon(id) : std::nullopt;
    row.image = source.image ? source.image(id) : nullptr;
    row.depth = depth;
    row.has_children = source.has_children(id);
    row.expanded = expanded.contains(id);
    row.selected = selected.contains(id);
    row.checked = source.checked ? source.checked(id) : std::nullopt;
    out.push_back(row);

    if (row.has_children && row.expanded) {
        size_t n = source.child_count(id);
        for (size_t i = 0; i < n; ++i)
            flatten_node(source, source.child_at(id, i), depth + 1, expanded, selected, out);
    }
}
} // namespace tree_detail

inline std::vector<TreeRow> visible_rows(const TreeSource& source,
                                          const std::set<TreeNodeId>& expanded,
                                          const std::set<TreeNodeId>& selected = {}) {
    std::vector<TreeRow> rows;
    size_t n = source.root_count();
    for (size_t i = 0; i < n; ++i)
        tree_detail::flatten_node(source, source.root_at(i), 0, expanded, selected, rows);
    return rows;
}

// Where a dragged tree node goes, relative to the node it is dropped on.
enum class TreeDrop { Before, After, Inside };

class TreeController {
public:
    explicit TreeController(TreeSource source) : source_(std::move(source)) {
        // The app setting `selected` selects that node alone (the controller's own moves go
        // through set_current and keep the selection they make).
        follow_ = selected.on_change().connect([this](const std::optional<TreeNodeId>& s) {
            if (setting_current_) return;
            selection.set(s ? std::vector<TreeNodeId>{*s} : std::vector<TreeNodeId>{});
            refresh();
        });
        refresh();
    }

    List<TreeRow> rows;
    Field<std::optional<TreeNodeId>> selected;          // the current node (keys move from it)
    Field<std::vector<TreeNodeId>> selection;           // every selected node, in the rows' order
    Field<std::optional<TreeDetail>> detail;
    // Set it to let the user drag nodes (doc/design/drag-and-docking.md): called with the node,
    // the node it was dropped on and where; the app moves it in its data, then the rows refresh.
    std::function<void(TreeNodeId node, TreeNodeId target, TreeDrop where)> on_move;

    // A drop on the middle half of a node that can hold children goes inside it; nearer an
    // edge, before or after it.
    [[nodiscard]] static TreeDrop drop_zone(const TreeRow& target, float y, float height) {
        if (target.has_children && y > height * 0.25f && y < height * 0.75f) return TreeDrop::Inside;
        return y < height / 2 ? TreeDrop::Before : TreeDrop::After;
    }

    // Whether row `from` may be dropped on row `to`: not on itself, not inside its own subtree
    // (the rows right after it that are deeper).
    [[nodiscard]] bool can_move(std::size_t from, std::size_t to) const {
        if (!on_move || from >= rows.size() || to >= rows.size() || from == to) return false;
        if (to < from) return true;
        for (std::size_t i = from + 1; i <= to; ++i)
            if (rows[i].depth <= rows[from].depth) return true;
        return false;
    }

    void move_row(std::size_t from, std::size_t to, TreeDrop where) {
        if (!can_move(from, to)) return;
        on_move(rows[from].id, rows[to].id, where);
        refresh();
    }

    void refresh() {
        auto sel = selection.get();
        auto fresh = visible_rows(source_, expanded_, std::set<TreeNodeId>(sel.begin(), sel.end()));
        if (rename_)
            for (auto& r : fresh)
                if (r.id == rename_->first) r.rename = rename_->second;
        while (!rows.empty()) rows.erase(rows.size() - 1);
        for (auto& r : fresh) rows.push_back(r);
    }

    [[nodiscard]] bool renaming() const { return rename_.has_value(); }

    // A click on row `index` at `mb` (row-local): on the checkbox it toggles it; a double-click
    // renames the node when the source can; anything else selects (on_row_clicked). A click on
    // another row ends a rename first.
    // simplify: a click inside the editor does not move the caret; it is ignored.
    void on_row_click(size_t index, const MouseButton& mb) {
        if (index >= rows.size()) return;
        auto row = rows[index];
        if (rename_ && rename_->first == row.id) return;
        if (rename_ && !commit_rename()) cancel_rename();
        if (Widget<TreeRow>::on_check(row, mb.position.x.raw())) return toggle_check(row.id);
        if (mb.clicks >= 2 && source_.rename) {
            anchor_ = index;
            set_current(row.id);
            selection.set({row.id});
            return begin_rename(row.id);
        }
        on_row_clicked(index, row, mb.mods);
    }

    // Text typed or pasted while renaming.
    void on_text(std::string_view text) {
        if (!rename_) return;
        rename_->second.text = text_edit::insert(rename_->second.text, text, 0);
        rename_->second.rejected = false;
        show_rename();
    }

    void begin_rename(TreeNodeId id) {
        if (!source_.rename) return;
        rename_.emplace(id, TreeRename{text_edit::select_all(TextEdit{source_.label(id)})});
        refresh();
    }

    void cancel_rename() {
        rename_.reset();
        refresh();
    }

    // Hands the name to the source: true and closed if it took it, false and marked if not.
    bool commit_rename() {
        if (!rename_) return true;
        auto& [id, r] = *rename_;
        if (!source_.rename(id, r.text.text)) {
            r.rejected = true;
            show_rename();
            return false;
        }
        rename_.reset();
        refresh();
        return true;
    }

    void toggle_check(TreeNodeId id) {
        if (!source_.checked || !source_.set_checked) return;
        if (auto on = source_.checked(id)) source_.set_checked(id, !*on);
        refresh();
    }

    // A click on row `index`: Ctrl toggles it in the selection, Shift selects the range from the
    // last click, a plain click selects it alone (and opens or closes it).
    void on_row_clicked(size_t index, const TreeRow& row, std::uint16_t m = 0) {
        if (mods::shortcut(m)) {
            toggle(row.id);
            anchor_ = index;
            set_current(row.id);
            refresh();
            return;
        }
        if (m & mods::shift) {
            select_range(index);
            refresh();
            return;
        }
        anchor_ = index;
        set_current(row.id);
        selection.set({row.id});
        if (row.has_children) {
            if (expanded_.contains(row.id)) expanded_.erase(row.id);
            else expanded_.insert(row.id);
        }
        populate_detail(row.id);
        refresh();
    }

    // Returns the index in `rows` that should be scrolled into view, if selection moved or
    // an expand/collapse happened; nullopt if `ev` wasn't a handled KeyPress.
    std::optional<size_t> on_key(const InputEvent& ev) {
        auto* kp = std::get_if<KeyPress>(&ev);
        if (!kp) return std::nullopt;
        if (rename_) {
            rename_key(*kp);
            return std::nullopt;
        }

        std::vector<TreeRow> snapshot;
        for (size_t i = 0; i < rows.size(); ++i) snapshot.push_back(rows[i]);
        if (snapshot.empty()) return std::nullopt;

        auto sel = selected.get();
        size_t cur = 0;
        bool has_cur = false;
        if (sel) {
            for (size_t i = 0; i < snapshot.size(); ++i)
                if (snapshot[i].id == *sel) { cur = i; has_cur = true; break; }
        }

        bool extend = (kp->mods & mods::shift) != 0;
        if (kp->key == keys::down || kp->key == keys::up) {
            bool down = kp->key == keys::down;
            if (!down && (!has_cur || cur == 0)) return std::nullopt;
            size_t next = down ? (has_cur ? cur + 1 : 0) : cur - 1;
            if (next >= snapshot.size()) return std::nullopt;
            if (extend && has_cur) {
                if (!anchor_) anchor_ = cur;
                select_range(next);
                refresh();
            } else {
                anchor_ = next;
                select_row(snapshot[next].id);
            }
            return next;
        }
        if (kp->key == keys::right) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.has_children && !row.expanded) {
                expanded_.insert(row.id);
                refresh();
                return cur;
            }
            if (row.expanded && cur + 1 < snapshot.size()) {
                select_row(snapshot[cur + 1].id);
                return cur + 1;
            }
            return std::nullopt;
        }
        if (kp->key == keys::left) {
            if (!has_cur) return std::nullopt;
            auto& row = snapshot[cur];
            if (row.expanded) {
                expanded_.erase(row.id);
                refresh();
                return cur;
            }
            for (size_t i = cur; i-- > 0; ) {
                if (snapshot[i].depth < row.depth) {
                    select_row(snapshot[i].id);
                    return i;
                }
            }
            return std::nullopt;
        }
        if (kp->key == keys::f2) {
            if (!has_cur) return std::nullopt;
            begin_rename(snapshot[cur].id);
            return cur;
        }
        if (kp->key == keys::space && has_cur && snapshot[cur].checked) {
            toggle_check(snapshot[cur].id);
            return cur;
        }
        if (kp->key == keys::enter || kp->key == keys::space) {
            if (!has_cur) return std::nullopt;
            on_row_clicked(cur, snapshot[cur]);
            return cur;
        }
        return std::nullopt;
    }

private:
    // Keys while renaming: Enter commits, Escape cancels, the rest edit the text. Every key
    // stays in the editor (the container keeps them, see vb.tree).
    void rename_key(const KeyPress& kp) {
        auto& e = rename_->second.text;
        if (kp.key == keys::enter) commit_rename();
        else if (kp.key == keys::escape) cancel_rename();
        else if (kp.key == keys::home || kp.key == keys::end) {
            e = text_edit::move_to(e, kp.key == keys::home ? 0 : e.text.size(), (kp.mods & mods::shift) != 0);
            show_rename();
        } else if (auto after = text_detail::common_edit_key(e, kp)) {
            e = *after;
            show_rename();
        }
    }

    // Puts the editor's state into its row alone (a keystroke changes one row).
    void show_rename() {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].id != rename_->first) continue;
            auto r = rows[i];
            r.rename = rename_->second;
            rows.set(i, r);
        }
    }

    void select_row(TreeNodeId id) {
        set_current(id);
        selection.set({id});
        populate_detail(id);
        refresh();
    }

    void set_current(TreeNodeId id) {
        setting_current_ = true;
        selected.set(id);
        setting_current_ = false;
    }

    void toggle(TreeNodeId id) {
        auto s = selection.get();
        if (std::erase(s, id) == 0) s.push_back(id);
        selection.set(in_row_order(std::move(s)));
    }

    // The rows from the anchor (the last plain or Ctrl click) to `to`, both included; `to`
    // becomes the current node.
    void select_range(size_t to) {
        if (to >= rows.size()) return;
        size_t from = anchor_.value_or(to);
        if (from >= rows.size()) from = to;
        std::vector<TreeNodeId> s;
        for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) s.push_back(rows[i].id);
        selection.set(std::move(s));
        set_current(rows[to].id);
    }

    // `ids` sorted as the rows show them (ids not shown keep their place at the end).
    std::vector<TreeNodeId> in_row_order(std::vector<TreeNodeId> ids) const {
        auto pos = [&](TreeNodeId id) {
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].id == id) return i;
            return rows.size();
        };
        std::ranges::stable_sort(ids, {}, pos);
        return ids;
    }

    void populate_detail(TreeNodeId id) {
        TreeDetail d;
        d.label = source_.label(id);
        d.attributes = source_.attributes ? source_.attributes(id)
                                           : std::vector<std::pair<std::string, std::string>>{};
        detail.set(d);
    }

    TreeSource source_;
    std::set<TreeNodeId> expanded_;
    std::optional<size_t> anchor_;   // the row a Shift-click or Shift+arrow range starts from
    std::optional<std::pair<TreeNodeId, TreeRename>> rename_;
    bool setting_current_ = false;
    Connection follow_;
};

#if __cpp_impl_reflection

namespace tree_detail {

// Matches std::optional<X> where X is a reflectable class (excluding std::string, which is
// itself a class type but must be treated as a plain value, not something to recurse into).
template <typename T>
struct IsOptionalOfClass : std::false_type {};
template <typename X>
struct IsOptionalOfClass<std::optional<X>>
    : std::bool_constant<std::is_class_v<X> && !std::is_same_v<X, std::string>> {};

// std::vector is itself a class type, so it must be excluded explicitly from the
// "directly-nested class member" branch below -- otherwise it would be misclassified as an
// always-present child slot and recursed into via reflection on its (implementation-defined)
// internal layout, instead of being skipped per the documented container-of-children non-goal.
template <typename T>
struct IsVector : std::false_type {};
template <typename X>
struct IsVector<std::vector<X>> : std::true_type {};

// Recursively classifies every member of X: a pointer/std::optional to a reflectable class is
// a child slot (descend only if non-null); a directly-nested reflectable class member is always
// a child slot; everything else (int, string, enum, bool, ...) becomes an attribute, never a
// tree row. std::vector<...> members (including vector-of-reflectable-class -- the documented
// v1 non-goal) match none of these branches and are silently skipped.
//
// Node identity is a monotonically-increasing counter (`next_id`), NOT the object's address.
// Addresses collide whenever a nested-class member sits at offset 0 of its parent -- in a
// standard-layout struct, `&outer == &outer.inner` when `inner` is the first member -- which
// would make the parent and that child share one cache slot and silently drop whichever entry
// lost the race to `cache.emplace()`. A fresh id assigned at the start of each call, threaded
// through the recursion, has no such collision no matter how members are laid out.
//
// `entry` is built up as a LOCAL value and inserted into `cache` only once, at the very end --
// never held as a reference across the recursive calls below. cache[id] / cache.emplace(...)
// can rehash std::unordered_map and invalidate references to every other element; taking a
// reference up front and using it after a nested populate_struct_tree_cache() call (which also
// inserts into the same cache) would be a dangling-reference bug.
//
// No cycle detection (documented v1 non-goal): a self-referential structure recurses until a
// real leaf or the stack overflows.
template <typename X, typename Cache>
TreeNodeId populate_struct_tree_cache(X& obj, std::string_view label, Cache& cache,
                                       TreeNodeId& next_id) {
    TreeNodeId id = next_id++;

    typename Cache::mapped_type entry;
    entry.label = std::string(label);

    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^X, std::meta::access_context::unchecked()));

    template for (constexpr auto m : members) {
        auto member_name = std::meta::identifier_of(m);
        auto& member = obj.[:m:];
        using M = std::remove_cvref_t<decltype(member)>;

        if constexpr (std::is_pointer_v<M> && std::is_class_v<std::remove_pointer_t<M>>
                      && !std::is_same_v<std::remove_pointer_t<M>, std::string>) {
            if (member != nullptr) {
                entry.children.push_back(
                    populate_struct_tree_cache(*member, member_name, cache, next_id));
            }
        } else if constexpr (IsOptionalOfClass<M>::value) {
            if (member.has_value()) {
                entry.children.push_back(
                    populate_struct_tree_cache(*member, member_name, cache, next_id));
            }
        } else if constexpr (std::is_class_v<M> && !std::is_same_v<M, std::string>
                              && !IsVector<M>::value) {
            entry.children.push_back(
                populate_struct_tree_cache(member, member_name, cache, next_id));
        } else if constexpr (LeafType<M>) {
            entry.attributes.emplace_back(std::string(member_name), format_leaf_value(member));
        }
    }

    cache.emplace(id, std::move(entry));
    return id;
}

} // namespace tree_detail

template <typename T>
TreeSource wrap_struct_tree(T& root) {
    struct Entry {
        std::string label;
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<TreeNodeId> children;
    };
    auto cache = std::make_shared<std::unordered_map<TreeNodeId, Entry>>();
    auto next_id = std::make_shared<TreeNodeId>(1);
    TreeNodeId root_id
        = tree_detail::populate_struct_tree_cache(root, std::meta::identifier_of(^^T), *cache,
                                                   *next_id);

    TreeSource src;
    src.root_count = [] { return size_t{1}; };
    src.root_at = [root_id](size_t) { return root_id; };
    src.label = [cache](TreeNodeId id) { return cache->at(id).label; };
    src.has_children = [cache](TreeNodeId id) { return !cache->at(id).children.empty(); };
    src.child_count = [cache](TreeNodeId id) { return cache->at(id).children.size(); };
    src.child_at = [cache](TreeNodeId id, size_t i) { return cache->at(id).children[i]; };
    src.attributes = [cache](TreeNodeId id) { return cache->at(id).attributes; };
    return src;
}

#endif // __cpp_impl_reflection

} // namespace prism::ui
