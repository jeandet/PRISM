#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>

namespace prism::ui {

namespace menu_detail {

constexpr Height title_h{30.f};
constexpr Height item_h{28.f};
constexpr float pad = 8.f;
constexpr float font_size = 14.f;

using Items = std::vector<std::string>;

inline Size popup_size(const Items& items, const WidgetNode& n, float min_w) {
    float w = 0.f;
    for (const auto& item : items)
        w = std::max(w, text_width(node_theme(n).measurer, parse_mnemonic(item).text, font_size));
    return Size{Width{std::max(w + 2.f * pad, min_w)}, Height{item_h.raw() * static_cast<float>(items.size())}};
}

// Item under a point in the owner's coordinates. The popup may have been moved to stay
// inside the window, so this reads where it actually is.
inline std::optional<std::size_t> item_at(const Items& items, const WidgetNode& node, Point p) {
    if (!node.popup_area || !node.popup_area->contains(p)) return std::nullopt;
    auto i = static_cast<std::size_t>((p.y.raw() - node.popup_area->origin.y.raw()) / item_h.raw());
    return i < items.size() ? std::optional{i} : std::nullopt;
}

// Below `anchor`, left-aligned; routing flips it near the window edges (place_popup).
inline std::optional<std::size_t> item_for_key(const Items& items, std::int32_t key) {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (key != 0 && mnemonic_key(items[i]) == key) return i;
    return std::nullopt;
}

inline void open_at(const Items& items, WidgetNode& node, Rect anchor, float min_w) {
    Point below{anchor.origin.x, anchor.origin.y + DY{anchor.extent.h.raw()}};
    node.popup_area = Rect{below, popup_size(items, node, min_w)};
    node.popup_opened_from = anchor;
    node.get_or_create<MenuEditState>().highlighted = 0;
    node.dirty = true;
}

inline void close(WidgetNode& node) {
    node.popup_area.reset();
    node.popup_opened_from.reset();
    node.overlay_draws.clear();
    node.dirty = true;
}

template <typename M>
void choose(Field<M>& field, WidgetNode& node, std::size_t i) {
    close(node);
    auto m = field.get();
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

inline void draw_popup(const Items& items, WidgetNode& node) {
    const Rect& area = *node.popup_area;
    const auto& t = *node.theme;
    std::size_t hl = node.get_or_create<MenuEditState>().highlighted;
    node.overlay_draws.filled_rect(area, t.popup_bg);
    node.overlay_draws.rect_outline(area, t.popup_border, 1.f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        Y y{area.origin.y.raw() + static_cast<float>(i) * item_h.raw()};
        if (i == hl)
            node.overlay_draws.filled_rect(Rect{Point{area.origin.x, y}, Size{area.extent.w, item_h}}, t.popup_highlight);
        draw_label(node.overlay_draws, items[i], Point{area.origin.x + DX{pad}, y + DY{6.f}},
                   i == hl ? t.text_on_primary : t.text, t.measurer);
    }
}

// Input for an open popup: hover highlights, a left press on an item or Enter/Space chooses,
// Up/Down move. Returns false when the event is not the popup's.
template <typename M>
bool open_popup_input(Field<M>& field, const InputEvent& ev, WidgetNode& node) {
    const auto& items = field.get().items;
    auto& es = node.get_or_create<MenuEditState>();
    es.highlighted = std::min(es.highlighted, items.empty() ? 0 : items.size() - 1);   // items may have shrunk
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        auto i = item_at(items, node, mb->position);
        if (!i) return false;
        choose(field, node, *i);
    } else if (auto* mm = std::get_if<MouseMove>(&ev)) {
        if (auto i = item_at(items, node, mm->position); i && *i != es.highlighted) {
            es.highlighted = *i;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::down && es.highlighted + 1 < items.size()) ++es.highlighted;
        else if (kp->key == keys::up && es.highlighted > 0) --es.highlighted;
        else if ((kp->key == keys::enter || kp->key == keys::space) && es.highlighted < items.size())
            choose(field, node, es.highlighted);
        else if (auto i = item_for_key(items, kp->key); i && !mods::shortcut(kp->mods))
            choose(field, node, *i);
        else return false;
        node.dirty = true;
    } else {
        return false;
    }
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

} // namespace prism::ui
