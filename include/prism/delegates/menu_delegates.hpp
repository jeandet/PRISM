#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>

namespace prism::ui {

namespace menu_detail {

constexpr Height title_h{30.f};
constexpr Height item_h{28.f};
constexpr float pad = 8.f;
constexpr float font_size = 14.f;

inline float popup_width(const Menu& m, const WidgetNode& n) {
    float w = 0.f;
    for (const auto& item : m.items) w = std::max(w, text_width(node_theme(n).measurer, item, font_size));
    return std::max(w + 2.f * pad, delegate_detail::widget_w(n).raw());
}

// Row under a point in the menu's own coordinates, if any.
inline std::optional<std::size_t> item_at(const Menu& m, Point p) {
    float row = (p.y.raw() - title_h.raw()) / item_h.raw();
    if (row < 0.f) return std::nullopt;
    auto i = static_cast<std::size_t>(row);
    return i < m.items.size() ? std::optional{i} : std::nullopt;
}

inline void open(const Menu& m, WidgetNode& node) {
    node.popup_area = Rect{Point{X{0}, Y{title_h.raw()}},
                           Size{Width{popup_width(m, node)}, Height{item_h.raw() * static_cast<float>(m.items.size())}}};
    node.get_or_create<MenuEditState>().highlighted = 0;
    node.dirty = true;
}

inline void close(WidgetNode& node) {
    node.popup_area.reset();
    node.overlay_draws.clear();
    node.dirty = true;
}

inline void choose(Field<Menu>& field, WidgetNode& node, std::size_t i) {
    close(node);
    auto m = field.get();
    m.chosen = i;
    ++m.activations;
    field.set(m);
}

} // namespace menu_detail

inline float Widget<Menu>::intrinsic_width(const Field<Menu>& f, const WidgetNode& n) {
    return 2.f * menu_detail::pad + text_width(node_theme(n).measurer, f.get().title, menu_detail::font_size);
}

// simplify: the popup opens below the title and is not yet clamped or flipped at the window
// edges (the dropdown flips up; menus need both axes). Add with context menus.
inline void Widget<Menu>::record(DrawList& dl, const Field<Menu>& field, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    auto& vs = node_vs(node);
    auto& t = *node.theme;
    Width w = delegate_detail::widget_w(node);
    bool is_open = node.popup_area.has_value();
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, title_h),
                   is_open ? t.surface_active : vs.hovered ? t.surface_hover : t.surface);
    dl.text(m.title, delegate_detail::make_point(X{pad}, Y{7}), font_size, t.text);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, title_h, t);

    node.overlay_draws.clear();
    if (!is_open) return;
    const Rect& area = *node.popup_area;
    std::size_t hl = node.get_or_create<MenuEditState>().highlighted;
    node.overlay_draws.filled_rect(area, t.popup_bg);
    node.overlay_draws.rect_outline(area, t.popup_border, 1.f);
    for (std::size_t i = 0; i < m.items.size(); ++i) {
        Y y{area.origin.y.raw() + static_cast<float>(i) * item_h.raw()};
        if (i == hl)
            node.overlay_draws.filled_rect(Rect{Point{X{0}, y}, Size{area.extent.w, item_h}}, t.popup_highlight);
        node.overlay_draws.text(m.items[i], Point{X{pad}, y + DY{6.f}}, font_size,
                                i == hl ? t.text_on_primary : t.text);
    }
}

inline void Widget<Menu>::handle_input(Field<Menu>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace menu_detail;
    const auto& m = field.get();
    bool is_open = node.popup_area.has_value();
    auto& es = node.get_or_create<MenuEditState>();

    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        if (!is_open) open(m, node);
        else if (auto i = item_at(m, mb->position)) choose(field, node, *i);
        else close(node);   // the title again: toggle closed
    } else if (auto* mm = std::get_if<MouseMove>(&ev); mm && is_open) {
        if (auto i = item_at(m, mm->position); i && *i != es.highlighted) {
            es.highlighted = *i;
            node.dirty = true;
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        bool activate = kp->key == keys::enter || kp->key == keys::space;
        if (!is_open) {
            if (activate || kp->key == keys::down) open(m, node);
        } else if (kp->key == keys::down && es.highlighted + 1 < m.items.size()) {
            ++es.highlighted;
            node.dirty = true;
        } else if (kp->key == keys::up && es.highlighted > 0) {
            --es.highlighted;
            node.dirty = true;
        } else if (activate && es.highlighted < m.items.size()) {
            choose(field, node, es.highlighted);
        }
    }
}

} // namespace prism::ui
