#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>

namespace prism::ui {

namespace menu_detail {

constexpr Height title_h{30.f};
constexpr Height item_h{28.f};
constexpr Height separator_h{9.f};
constexpr float pad = 8.f;
constexpr float font_size = 14.f;
constexpr float shortcut_gap = 32.f;   // between the longest label and the shortcut column

using Items = std::vector<MenuItem>;

constexpr float icon_size = 16.f;
constexpr float icon_slot = icon_size + 8.f;   // the icon (or check mark) and its gap before the label

inline bool selectable(const MenuItem& item) { return !item.separator && item.enabled; }
inline Height row_h(const MenuItem& item) { return item.separator ? separator_h : item_h; }

float row_top(const Items& items, std::size_t i);

// Any icon or check mark at all: then every label moves right by the slot, so they stay aligned.
inline bool has_marks(const Items& items) {
    return std::ranges::any_of(items, [](const MenuItem& i) { return i.icon || i.checked; });
}

inline float label_offset(const Items& items) { return pad + (has_marks(items) ? icon_slot : 0.f); }

inline float widest(const Items& items, const TextMeasurer* m, auto text_of) {
    float w = 0.f;
    for (const auto& item : items)
        if (!item.separator) w = std::max(w, text_width(m, text_of(item), font_size));
    return w;
}

constexpr std::string_view submenu_arrow = "\xe2\x80\xba";   // ›

inline bool opens_submenu(const MenuItem& item) { return !item.separator && !item.items.empty(); }

// The column on the right: shortcuts, and the arrow of items that open a submenu.
float shortcut_column_w(const Items& items, const TextMeasurer* m);

Size popup_size(const Items& items, const WidgetNode& n, float min_w);

using Path = std::vector<std::size_t>;

// The items of the panel at `level`: the popup's (0), or a submenu's, opened from the item
// highlighted one level up.
const Items& panel_items(const Items& items, const Path& path, std::size_t level);

// Each open panel's place, in the owner's coordinates: the popup where it is (it may have been
// moved to stay inside the window), each submenu to the right of its item.
// simplify: a submenu always opens to the right, even past the window's edge; flip it to the
// left (as place_popup flips the popup) when a deep menu at the right edge needs it.
std::vector<Rect> panels(const Items& items, const WidgetNode& node, const Path& path);

// The panel and selectable item under a point; the panel alone when the point is on one of
// its separators or disabled items. Deepest panel first: submenus are drawn on top.
struct Hit {
    std::size_t level;
    std::optional<std::size_t> item;
};

std::optional<Hit> hit(const Items& items, const WidgetNode& node, const Path& path, Point p);

// The next selectable item after `from` in direction dir (+1 / -1), if any.
std::optional<std::size_t> step(const Items& items, std::size_t from, int dir);

std::size_t first_selectable(const Items& items);

std::optional<std::size_t> item_for_key(const Items& items, std::int32_t key);

// The item a shortcut chooses, submenus included.
std::optional<Path> path_for_shortcut(const Items& items, const KeyPress& kp);

// Keeps the highlight path valid for the items as they are now (the app may have changed
// them), and records where the submenus are, for routing (WidgetNode::popup_more).
void sync_panels(const Items& items, WidgetNode& node);

// Below `anchor`, left-aligned; routing flips it near the window edges (place_popup).
void open_at(const Items& items, WidgetNode& node, Rect anchor, float min_w);

void close(WidgetNode& node);

// Closes the menu and reports the item at `path`; a check item flips its mark first.
template <typename M>
void choose(Field<M>& field, WidgetNode& node, const Path& path) {
    auto m = field.get();
    Items* at = &m.items;
    MenuItem* item = nullptr;
    for (auto i : path) {
        if (i >= at->size() || !selectable((*at)[i])) return;
        item = &(*at)[i];
        at = &item->items;
    }
    if (!item || opens_submenu(*item)) return;
    close(node);
    if (auto& c = item->checked) c = !*c;
    m.chosen = path.front();
    m.chosen_path = path;
    ++m.activations;
    field.set(m);
}

// The user points at item `i` of the panel at `level` (hover, or keys): it is highlighted, and
// its submenu, if any, opens beside it.
void highlight(const Items& items, WidgetNode& node, std::size_t level, std::size_t i);

// Choosing item `i` of the panel at `level`: an item with a submenu opens it instead.
template <typename M>
void activate(Field<M>& field, WidgetNode& node, std::size_t level, std::size_t i) {
    const auto& items = field.get().items;
    auto path = node.get_or_create<MenuEditState>().path;
    path.resize(level + 1);
    path[level] = i;
    if (opens_submenu(panel_items(items, path, level)[i])) highlight(items, node, level, i);
    else choose(field, node, path);
}

// A label without its "&" marker, with the marked letter underlined.
void draw_label(DrawList& dl, std::string_view label, Point at, Color color, const TextMeasurer* m);

void draw_item(DrawList& dl, const Items& items, std::size_t i, Rect row, bool highlighted, const Theme& t);

void draw_panel(DrawList& dl, const Items& items, Rect area, std::size_t highlighted, const Theme& t);

void draw_popup(const Items& items, WidgetNode& node);

// Whether a pointer moving from `from` to `to` heads for the deepest open submenu across its
// parent panel: then the items it crosses on the way must not close the submenu. True when
// `to` is inside the triangle from `from` to the submenu's near edge (the "safe triangle").
bool heading_for_submenu(const std::vector<Rect>& rects, const Hit& h, Point from, Point to);

// Whether the open menu itself takes this arrow or Escape (a submenu to open or close), rather
// than routing (switching menus in a bar, closing the menu).
bool keeps_key(const WidgetNode& node, std::int32_t key);

inline bool popup_key(auto& field, const KeyPress& kp, WidgetNode& node) {
    const auto& items = field.get().items;
    auto& path = node.get_or_create<MenuEditState>().path;
    std::size_t level = path.size() - 1;
    const auto& here = panel_items(items, path, level);
    if (kp.key == keys::down || kp.key == keys::up) {
        if (auto i = step(here, path.back(), kp.key == keys::down ? +1 : -1)) path.back() = *i;
    } else if (kp.key == keys::right && keeps_key(node, kp.key)) {
        highlight(items, node, level, path.back());
    } else if ((kp.key == keys::left || kp.key == keys::escape) && level > 0) {
        path.pop_back();
    } else if (kp.key == keys::enter || kp.key == keys::space) {
        if (path.back() < here.size() && selectable(here[path.back()])) activate(field, node, level, path.back());
    } else if (auto i = item_for_key(here, kp.key); i && !mods::shortcut(kp.mods)) {
        activate(field, node, level, *i);
    } else {
        return false;
    }
    if (node.popup_area) sync_panels(items, node);
    node.dirty = true;
    return true;
}

// Input for an open popup: hover highlights (and opens a submenu), a left press on an item or
// Enter/Space chooses, Up/Down move past separators and disabled items, Right and Left open and
// close submenus. Returns false when the event is not the popup's.
template <typename M>
bool open_popup_input(Field<M>& field, const InputEvent& ev, WidgetNode& node) {
    const auto& items = field.get().items;
    sync_panels(items, node);
    const auto& path = node.get_or_create<MenuEditState>().path;
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        auto h = hit(items, node, path, in_popup(node, mb->position));
        if (!h) return false;
        if (h->item) activate(field, node, h->level, *h->item);
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        const Point at = in_popup(node, mm->position);
        auto h = hit(items, node, path, at);
        auto& es = node.get_or_create<MenuEditState>();
        auto from = std::exchange(es.pointer, at);
        bool on_the_way = h && from && heading_for_submenu(panels(items, node, path), *h, *from, at);
        if (h && h->item && !on_the_way && !(h->level < path.size() && path[h->level] == *h->item))
            highlight(items, node, h->level, *h->item);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        return popup_key(field, *kp, node);
    } else {
        return false;
    }
    return true;
}

// A shortcut chooses its item from anywhere, the menu closed or not.
template <typename M>
bool try_shortcut(Field<M>& field, const KeyPress& kp, WidgetNode& node) {
    auto path = path_for_shortcut(field.get().items, kp);
    if (!path) return false;
    choose(field, node, *path);
    return true;
}

} // namespace menu_detail

inline float Widget<Menu>::intrinsic_width(const Field<Menu>& f, const WidgetNode& n) {
    return 2.f * menu_detail::pad
         + text_width(node_theme(n).measurer, menu_detail::parse_mnemonic(f.get().title).text, menu_detail::font_size);
}

inline void Widget<ContextMenu>::record(DrawList&, const Field<ContextMenu>& field, WidgetNode& node) {
    node.overlay_draws.clear();
    if (node.popup_area) menu_detail::draw_popup(field.get().items, node);
}

// Calls `cb` each time the user chooses an item (click, keys or shortcut), and not when the app
// itself changes the menu (greys out or ticks an item). `cb` takes the item's index, and hears
// top-level items only; or it takes the path to the item (std::vector<std::size_t>), submenus
// included.
template <typename M, typename F>
Connection on_choice(Field<M>& menu, F cb) {
    // Shared: a signal calls a copy of its callback, so state inside the lambda would not stick.
    auto last = std::make_shared<std::uint64_t>(menu.get().activations);
    return menu.on_change().connect([cb = std::move(cb), last](const M& m) mutable {
        if (m.activations == *last) return;
        *last = m.activations;
        if constexpr (std::invocable<F&, const std::vector<std::size_t>&>) {
            if (!m.chosen_path.empty()) cb(m.chosen_path);
        } else if (m.chosen && m.chosen_path.size() <= 1) {
            cb(*m.chosen);
        }
    });
}

inline bool Widget<Menu>::try_shortcut(Field<Menu>& f, const KeyPress& kp, WidgetNode& n) {
    return menu_detail::try_shortcut(f, kp, n);
}

inline bool Widget<ContextMenu>::try_shortcut(Field<ContextMenu>& f, const KeyPress& kp, WidgetNode& n) {
    return menu_detail::try_shortcut(f, kp, n);
}

} // namespace prism::ui
