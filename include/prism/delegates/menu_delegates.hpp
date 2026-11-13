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

inline float row_top(const Items& items, std::size_t i) {
    float y = 0.f;
    for (std::size_t k = 0; k < i && k < items.size(); ++k) y += row_h(items[k]).raw();
    return y;
}

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
inline float shortcut_column_w(const Items& items, const TextMeasurer* m) {
    float w = widest(items, m, [](const MenuItem& i) {
        return opens_submenu(i) ? std::string(submenu_arrow) : shortcut_text(i.shortcut);
    });
    return w > 0.f ? shortcut_gap + w : 0.f;
}

inline Size popup_size(const Items& items, const WidgetNode& n, float min_w) {
    const auto* m = node_theme(n).measurer;
    float labels = widest(items, m, [](const MenuItem& i) { return parse_mnemonic(i.text).text; });
    return Size{Width{std::max(label_offset(items) + labels + shortcut_column_w(items, m) + pad, min_w)},
                Height{row_top(items, items.size())}};
}

using Path = std::vector<std::size_t>;

// The items of the panel at `level`: the popup's (0), or a submenu's, opened from the item
// highlighted one level up.
inline const Items& panel_items(const Items& items, const Path& path, std::size_t level) {
    const Items* at = &items;
    for (std::size_t k = 0; k < level; ++k) at = &(*at)[path[k]].items;
    return *at;
}

// Each open panel's place, in the owner's coordinates: the popup where it is (it may have been
// moved to stay inside the window), each submenu to the right of its item.
// simplify: a submenu always opens to the right, even past the window's edge; flip it to the
// left (as place_popup flips the popup) when a deep menu at the right edge needs it.
inline std::vector<Rect> panels(const Items& items, const WidgetNode& node, const Path& path) {
    std::vector<Rect> out{*node.popup_area};
    for (std::size_t level = 1; level < path.size(); ++level) {
        const Rect& up = out.back();
        const auto& above = panel_items(items, path, level - 1);
        Point at{up.origin.x + DX{up.extent.w.raw()}, up.origin.y + DY{row_top(above, path[level - 1])}};
        out.push_back(Rect{at, popup_size(panel_items(items, path, level), node, 0.f)});
    }
    return out;
}

// The panel and selectable item under a point; the panel alone when the point is on one of
// its separators or disabled items. Deepest panel first: submenus are drawn on top.
struct Hit {
    std::size_t level;
    std::optional<std::size_t> item;
};

inline std::optional<Hit> hit(const Items& items, const WidgetNode& node, const Path& path, Point p) {
    auto rects = panels(items, node, path);
    for (std::size_t level = rects.size(); level-- > 0;) {
        if (!rects[level].contains(p)) continue;
        const auto& its = panel_items(items, path, level);
        float y = p.y.raw() - rects[level].origin.y.raw();
        for (std::size_t i = 0; i < its.size(); ++i) {
            float top = row_top(its, i);
            if (y >= top && y < top + row_h(its[i]).raw())
                return Hit{level, selectable(its[i]) ? std::optional{i} : std::nullopt};
        }
        return Hit{level, std::nullopt};
    }
    return std::nullopt;
}

// The next selectable item after `from` in direction dir (+1 / -1), if any.
inline std::optional<std::size_t> step(const Items& items, std::size_t from, int dir) {
    for (auto i = static_cast<std::ptrdiff_t>(from) + dir; i >= 0 && i < std::ssize(items); i += dir)
        if (selectable(items[static_cast<std::size_t>(i)])) return static_cast<std::size_t>(i);
    return std::nullopt;
}

inline std::size_t first_selectable(const Items& items) {
    if (!items.empty() && selectable(items[0])) return 0;
    return step(items, 0, +1).value_or(0);
}

inline std::optional<std::size_t> item_for_key(const Items& items, std::int32_t key) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (key != 0 && selectable(items[i]) && mnemonic_key(items[i].text) == key) return i;
    return std::nullopt;
}

// The item a shortcut chooses, submenus included.
inline std::optional<Path> path_for_shortcut(const Items& items, const KeyPress& kp) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!selectable(items[i])) continue;
        if (opens_submenu(items[i])) {
            if (auto sub = path_for_shortcut(items[i].items, kp)) {
                sub->insert(sub->begin(), i);
                return sub;
            }
        } else if (matches(items[i].shortcut, kp)) {
            return Path{i};
        }
    }
    return std::nullopt;
}

// Keeps the highlight path valid for the items as they are now (the app may have changed
// them), and records where the submenus are, for routing (WidgetNode::popup_more).
inline void sync_panels(const Items& items, WidgetNode& node) {
    auto& es = node.get_or_create<MenuEditState>();
    if (es.path.empty()) es.path = {first_selectable(items)};
    const Items* at = &items;
    for (std::size_t level = 0; level < es.path.size(); ++level) {
        if (at->empty()) { es.path.resize(level); break; }
        es.path[level] = std::min(es.path[level], at->size() - 1);
        if (level + 1 < es.path.size()) {
            const auto& opener = (*at)[es.path[level]];
            if (!selectable(opener) || !opens_submenu(opener)) { es.path.resize(level + 1); break; }
            at = &opener.items;
        }
    }
    if (es.path.empty()) es.path = {0};
    const auto& last = panel_items(items, es.path, es.path.size() - 1);
    es.opener_highlighted = es.path.back() < last.size() && selectable(last[es.path.back()])
                         && opens_submenu(last[es.path.back()]);
    auto rects = panels(items, node, es.path);
    node.popup_more.assign(rects.begin() + 1, rects.end());
}

// Below `anchor`, left-aligned; routing flips it near the window edges (place_popup).
inline void open_at(const Items& items, WidgetNode& node, Rect anchor, float min_w) {
    Point below{anchor.origin.x, anchor.origin.y + DY{anchor.extent.h.raw()}};
    node.popup_area = Rect{below, popup_size(items, node, min_w)};
    node.popup_opened_from = anchor;
    node.get_or_create<MenuEditState>() = MenuEditState{.path = {first_selectable(items)}};
    sync_panels(items, node);
    node.dirty = true;
}

inline void close(WidgetNode& node) {
    node.popup_area.reset();
    node.popup_opened_from.reset();
    node.popup_more.clear();
    node.overlay_draws.clear();
    node.dirty = true;
}

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
inline void highlight(const Items& items, WidgetNode& node, std::size_t level, std::size_t i) {
    auto& path = node.get_or_create<MenuEditState>().path;
    path.resize(level + 1);
    path[level] = i;
    const auto& item = panel_items(items, path, level)[i];
    if (opens_submenu(item)) path.push_back(first_selectable(item.items));
    sync_panels(items, node);
    node.dirty = true;
}

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
inline void draw_label(DrawList& dl, std::string_view label, Point at, Color color, const TextMeasurer* m) {
    auto mn = parse_mnemonic(label);
    dl.text(mn.text, at, font_size, color);
    if (mnemonic_key(label) == 0) return;   // no key can trigger it: do not promise one
    std::string_view text = mn.text;
    float x0 = text_width(m, text.substr(0, mn.underline), font_size);
    float x1 = text_width(m, text.substr(0, core::utf8::next(text, mn.underline)), font_size);
    Y under = at.y + DY{font_size + 2.f};
    dl.line(Point{at.x + DX{x0}, under}, Point{at.x + DX{x1}, under}, color, 1.f);
}

inline void draw_item(DrawList& dl, const Items& items, std::size_t i, Rect row, bool highlighted, const Theme& t) {
    const MenuItem& item = items[i];
    if (item.separator) {
        Y mid = row.origin.y + DY{row.extent.h.raw() / 2};
        dl.line(Point{row.origin.x + DX{pad}, mid}, Point{row.origin.x + DX{row.extent.w.raw() - pad}, mid}, t.popup_border, 1.f);
        return;
    }
    if (highlighted) dl.filled_rect(row, t.popup_highlight);
    Color color = !item.enabled ? t.text_placeholder : highlighted ? t.text_on_primary : t.text;
    X slot = row.origin.x + DX{pad};
    Y icon_y = row.origin.y + DY{(item_h.raw() - icon_size) / 2};
    if (item.icon) dl.image(item.icon, Rect{Point{slot, icon_y}, Size{Width{icon_size}, Height{icon_size}}}, icon_tint(item.icon, color));
    else if (item.checked.value_or(false)) dl.text("\xe2\x9c\x93", Point{slot + DX{2.f}, row.origin.y + DY{6.f}}, font_size, color);
    draw_label(dl, item.text, Point{row.origin.x + DX{label_offset(items)}, row.origin.y + DY{6.f}}, color, t.measurer);
    auto right = opens_submenu(item) ? std::string(submenu_arrow) : shortcut_text(item.shortcut);
    if (!right.empty()) {
        float w = text_width(t.measurer, right, font_size);
        dl.text(right, Point{row.origin.x + DX{row.extent.w.raw() - pad - w}, row.origin.y + DY{6.f}}, font_size,
                item.enabled && !highlighted ? t.text_muted : color);
    }
}

inline void draw_panel(DrawList& dl, const Items& items, Rect area, std::size_t highlighted, const Theme& t) {
    dl.filled_rect(area, t.popup_bg);
    dl.rect_outline(area, t.popup_border, 1.f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        Rect row{Point{area.origin.x, area.origin.y + DY{row_top(items, i)}}, Size{area.extent.w, row_h(items[i])}};
        draw_item(dl, items, i, row, i == highlighted && selectable(items[i]), t);
    }
}

inline void draw_popup(const Items& items, WidgetNode& node) {
    sync_panels(items, node);
    const auto& path = node.get_or_create<MenuEditState>().path;
    auto rects = panels(items, node, path);
    for (std::size_t level = 0; level < rects.size(); ++level)
        draw_panel(node.overlay_draws, panel_items(items, path, level), rects[level], path[level], *node.theme);
}

// Whether a pointer moving from `from` to `to` heads for the deepest open submenu across its
// parent panel: then the items it crosses on the way must not close the submenu. True when
// `to` is inside the triangle from `from` to the submenu's near edge (the "safe triangle").
inline bool heading_for_submenu(const std::vector<Rect>& rects, const Hit& h, Point from, Point to) {
    if (rects.size() < 2 || h.level != rects.size() - 2) return false;
    const Rect& sub = rects.back();
    float ax = from.x.raw(), ay = from.y.raw();
    float bx = sub.origin.x.raw(), by = sub.origin.y.raw();
    float cy = by + sub.extent.h.raw();
    auto side = [&](float x1, float y1, float x2, float y2) {
        return (x2 - x1) * (to.y.raw() - y1) - (y2 - y1) * (to.x.raw() - x1);
    };
    float d1 = side(ax, ay, bx, by), d2 = side(bx, by, bx, cy), d3 = side(bx, cy, ax, ay);
    bool has_neg = d1 < 0 || d2 < 0 || d3 < 0, has_pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(has_neg && has_pos);
}

// Whether the open menu itself takes this arrow or Escape (a submenu to open or close), rather
// than routing (switching menus in a bar, closing the menu).
inline bool keeps_key(const WidgetNode& node, std::int32_t key) {
    auto* es = std::any_cast<MenuEditState>(&node.edit_state);
    if (!es) return false;
    if (key == keys::right) return es->opener_highlighted;
    if (key == keys::left || key == keys::escape) return es->path.size() > 1;
    return false;
}

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
        auto h = hit(items, node, path, mb->position);
        if (!h) return false;
        if (h->item) activate(field, node, h->level, *h->item);
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        auto h = hit(items, node, path, mm->position);
        auto& es = node.get_or_create<MenuEditState>();
        auto from = std::exchange(es.pointer, mm->position);
        bool on_the_way = h && from && heading_for_submenu(panels(items, node, path), *h, *from, mm->position);
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

inline void Widget<Menu>::record(DrawList& dl, const Field<Menu>& field, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    auto& vs = node_vs(node);
    auto& t = *node.theme;
    Width w = delegate_detail::widget_w(node);
    bool is_open = node.popup_area.has_value();
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, title_h),
                   is_open ? t.surface_active : vs.hovered ? t.surface_hover : t.surface);
    draw_label(dl, m.title, delegate_detail::make_point(X{pad}, Y{7}), t.text, t.measurer);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, title_h, t);
    node.overlay_draws.clear();
    if (is_open) draw_popup(m.items, node);
}

inline void Widget<Menu>::handle_input(Field<Menu>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    auto open = [&] {
        Width w = delegate_detail::widget_w(node);
        open_at(m.items, node, Rect{Point{X{0}, Y{0}}, Size{w, title_h}}, w.raw());
    };
    if (node.popup_area && open_popup_input(field, ev, node)) return;
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        if (node.popup_area) close(node);   // the title again: toggle closed
        else open();
    } else if (auto* kp = std::get_if<KeyPress>(&ev); kp && !node.popup_area) {
        if (kp->key == keys::enter || kp->key == keys::space || kp->key == keys::down) open();
    }
}

inline void Widget<ContextMenu>::record(DrawList&, const Field<ContextMenu>& field, WidgetNode& node) {
    node.overlay_draws.clear();
    if (node.popup_area) menu_detail::draw_popup(field.get().items, node);
}

inline void Widget<ContextMenu>::handle_input(Field<ContextMenu>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace menu_detail;
    if (node.popup_area) {
        open_popup_input(field, ev, node);
        return;
    }
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::right)
        open_at(field.get().items, node, Rect{mb->position, Size{Width{0}, Height{0}}}, 120.f);
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
