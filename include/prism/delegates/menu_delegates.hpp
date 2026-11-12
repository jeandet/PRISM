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

inline float shortcut_column_w(const Items& items, const TextMeasurer* m) {
    float w = widest(items, m, [](const MenuItem& i) { return shortcut_text(i.shortcut); });
    return w > 0.f ? shortcut_gap + w : 0.f;
}

inline Size popup_size(const Items& items, const WidgetNode& n, float min_w) {
    const auto* m = node_theme(n).measurer;
    float labels = widest(items, m, [](const MenuItem& i) { return parse_mnemonic(i.text).text; });
    return Size{Width{std::max(label_offset(items) + labels + shortcut_column_w(items, m) + pad, min_w)},
                Height{row_top(items, items.size())}};
}

// The selectable item under a point in the owner's coordinates. The popup may have been moved
// to stay inside the window, so this reads where it actually is.
inline std::optional<std::size_t> item_at(const Items& items, const WidgetNode& node, Point p) {
    if (!node.popup_area || !node.popup_area->contains(p)) return std::nullopt;
    float y = p.y.raw() - node.popup_area->origin.y.raw();
    for (std::size_t i = 0; i < items.size(); ++i) {
        float top = row_top(items, i);
        if (y >= top && y < top + row_h(items[i]).raw()) return selectable(items[i]) ? std::optional{i} : std::nullopt;
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

inline std::optional<std::size_t> item_for_shortcut(const Items& items, const KeyPress& kp) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (selectable(items[i]) && matches(items[i].shortcut, kp)) return i;
    return std::nullopt;
}

// Below `anchor`, left-aligned; routing flips it near the window edges (place_popup).
inline void open_at(const Items& items, WidgetNode& node, Rect anchor, float min_w) {
    Point below{anchor.origin.x, anchor.origin.y + DY{anchor.extent.h.raw()}};
    node.popup_area = Rect{below, popup_size(items, node, min_w)};
    node.popup_opened_from = anchor;
    node.get_or_create<MenuEditState>().highlighted = first_selectable(items);
    node.dirty = true;
}

inline void close(WidgetNode& node) {
    node.popup_area.reset();
    node.popup_opened_from.reset();
    node.overlay_draws.clear();
    node.dirty = true;
}

// Closes the menu and reports item i; a check item flips its mark first.
template <typename M>
void choose(Field<M>& field, WidgetNode& node, std::size_t i) {
    auto m = field.get();
    if (i >= m.items.size() || !selectable(m.items[i])) return;
    close(node);
    if (auto& c = m.items[i].checked) c = !*c;
    m.chosen = i;
    ++m.activations;
    field.set(m);
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
    if (auto keys = shortcut_text(item.shortcut); !keys.empty()) {
        float w = text_width(t.measurer, keys, font_size);
        dl.text(keys, Point{row.origin.x + DX{row.extent.w.raw() - pad - w}, row.origin.y + DY{6.f}}, font_size,
                item.enabled && !highlighted ? t.text_muted : color);
    }
}

inline void draw_popup(const Items& items, WidgetNode& node) {
    const Rect& area = *node.popup_area;
    const auto& t = *node.theme;
    std::size_t hl = node.get_or_create<MenuEditState>().highlighted;
    node.overlay_draws.filled_rect(area, t.popup_bg);
    node.overlay_draws.rect_outline(area, t.popup_border, 1.f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        Rect row{Point{area.origin.x, area.origin.y + DY{row_top(items, i)}}, Size{area.extent.w, row_h(items[i])}};
        draw_item(node.overlay_draws, items, i, row, i == hl && selectable(items[i]), t);
    }
}

// Input for an open popup: hover highlights, a left press on an item or Enter/Space chooses,
// Up/Down move past separators and disabled items. Returns false when the event is not the
// popup's.
template <typename M>
bool open_popup_input(Field<M>& field, const InputEvent& ev, WidgetNode& node) {
    const auto& items = field.get().items;
    auto& es = node.get_or_create<MenuEditState>();
    es.highlighted = std::min(es.highlighted, items.empty() ? 0 : items.size() - 1);   // items may have shrunk
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        if (!node.popup_area->contains(mb->position)) return false;
        if (auto i = item_at(items, node, mb->position)) choose(field, node, *i);
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        if (auto i = item_at(items, node, mm->position); i && *i != es.highlighted) {
            es.highlighted = *i;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::down || kp->key == keys::up) {
            if (auto i = step(items, es.highlighted, kp->key == keys::down ? +1 : -1)) es.highlighted = *i;
        } else if (kp->key == keys::enter || kp->key == keys::space) {
            choose(field, node, es.highlighted);
        } else if (auto i = item_for_key(items, kp->key); i && !mods::shortcut(kp->mods)) {
            choose(field, node, *i);
        } else {
            return false;
        }
        node.dirty = true;
    } else {
        return false;
    }
    return true;
}

// A shortcut chooses its item from anywhere, the menu closed or not.
template <typename M>
bool try_shortcut(Field<M>& field, const KeyPress& kp, WidgetNode& node) {
    auto i = item_for_shortcut(field.get().items, kp);
    if (!i) return false;
    choose(field, node, *i);
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

// Calls `cb` with the chosen item's index each time the user chooses one (click, keys or
// shortcut), and not when the app itself changes the menu (greys out or ticks an item).
template <typename M>
Connection on_choice(Field<M>& menu, std::function<void(std::size_t)> cb) {
    // Shared: a signal calls a copy of its callback, so state inside the lambda would not stick.
    auto last = std::make_shared<std::uint64_t>(menu.get().activations);
    return menu.on_change().connect([cb = std::move(cb), last](const M& m) {
        if (m.activations == *last) return;
        *last = m.activations;
        if (m.chosen) cb(*m.chosen);
    });
}

inline bool Widget<Menu>::try_shortcut(Field<Menu>& f, const KeyPress& kp, WidgetNode& n) {
    return menu_detail::try_shortcut(f, kp, n);
}

inline bool Widget<ContextMenu>::try_shortcut(Field<ContextMenu>& f, const KeyPress& kp, WidgetNode& n) {
    return menu_detail::try_shortcut(f, kp, n);
}

} // namespace prism::ui
