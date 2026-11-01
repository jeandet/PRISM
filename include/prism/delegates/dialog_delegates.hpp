#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <vector>

namespace prism::ui {

namespace dialog_detail {

constexpr float pad = 20.f;
constexpr float title_size = 16.f;
constexpr float text_size = 14.f;
constexpr float button_h = 30.f;
constexpr float button_pad = 16.f;
constexpr float button_gap = 8.f;
constexpr float min_w = 320.f;
constexpr float card_h = pad + 24.f + 30.f + button_h + pad;   // title, message, buttons

// The window in the anchor's own coordinates.
inline Rect window_rect(const WidgetNode& node) {
    return Rect{Point{X{-node.absolute_x.raw()}, Y{-node.absolute_y.raw()}},
                Size{node.viewport_width, node.viewport_height}};
}

inline float button_w(const Dialog& d, const WidgetNode& node, std::size_t i) {
    return std::max(72.f, text_width(node_theme(node).measurer, d.buttons[i], text_size) + 2.f * button_pad);
}

inline Rect card_rect(const Dialog& d, const WidgetNode& node) {
    const auto* m = node_theme(node).measurer;
    float buttons = 0.f;
    for (std::size_t i = 0; i < d.buttons.size(); ++i) buttons += button_w(d, node, i) + button_gap;
    float w = std::max({min_w, buttons + 2.f * pad,
                        text_width(m, d.title, title_size) + 2.f * pad,
                        text_width(m, d.message, text_size) + 2.f * pad});
    Rect win = window_rect(node);
    return Rect{Point{X{win.origin.x.raw() + (win.extent.w.raw() - w) / 2.f},
                      Y{win.origin.y.raw() + (win.extent.h.raw() - card_h) / 2.f}},
                Size{Width{w}, Height{card_h}}};
}

// Button rects in the anchor's coordinates, right-aligned along the card's bottom.
inline std::vector<Rect> button_rects(const Dialog& d, const WidgetNode& node, const Rect& card) {
    std::vector<Rect> rects(d.buttons.size(), Rect{});
    float right = card.origin.x.raw() + card.extent.w.raw() - pad;
    float top = card.origin.y.raw() + card.extent.h.raw() - pad - button_h;
    for (std::size_t i = d.buttons.size(); i-- > 0;) {
        float w = button_w(d, node, i);
        right -= w;
        rects[i] = Rect{Point{X{right}, Y{top}}, Size{Width{w}, Height{button_h}}};
        right -= button_gap;
    }
    return rects;
}

inline void close(Field<Dialog>& field, WidgetNode& node, std::optional<std::size_t> choice) {
    node.popup_area.reset();
    node.overlay_draws.clear();
    node.edit_state.reset();   // the next opening starts on the first button
    node.dirty = true;
    auto d = field.get();
    d.open = false;
    d.chosen = choice;
    ++d.activations;
    field.set(d);
}

inline void draw(const Dialog& d, WidgetNode& node, const Rect& card) {
    const auto& t = *node.theme;
    auto& dl = node.overlay_draws;
    std::size_t hl = node.get_or_create<DialogEditState>().highlighted;
    dl.filled_rect(window_rect(node), t.scrim);
    dl.rounded_rect(card, t.popup_bg, 8.f);
    dl.rounded_rect(card, t.popup_border, 8.f, 1.f);
    dl.text(d.title, Point{card.origin.x + DX{pad}, card.origin.y + DY{pad}}, title_size, t.text);
    dl.text(d.message, Point{card.origin.x + DX{pad}, card.origin.y + DY{pad + 30.f}}, text_size, t.text);
    auto rects = button_rects(d, node, card);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        dl.rounded_rect(rects[i], i == hl ? t.primary : t.surface, 4.f);
        float tw = text_width(node_theme(node).measurer, d.buttons[i], text_size);
        dl.text(d.buttons[i],
                Point{rects[i].origin.x + DX{(rects[i].extent.w.raw() - tw) / 2.f}, rects[i].origin.y + DY{7.f}},
                text_size, i == hl ? t.text_on_primary : t.text);
    }
}

} // namespace dialog_detail

inline void Widget<Dialog>::record(DrawList&, const Field<Dialog>& field, WidgetNode& node) {
    using namespace dialog_detail;
    const auto& d = field.get();
    node.overlay_draws.clear();
    if (!d.open) {
        node.popup_area.reset();
        return;
    }
    Rect card = card_rect(d, node);
    node.popup_area = card;
    node.popup_modal = true;
    draw(d, node, card);
}

inline void Widget<Dialog>::handle_input(Field<Dialog>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace dialog_detail;
    const auto& d = field.get();
    if (!d.open || !node.popup_area) return;
    auto& es = node.get_or_create<DialogEditState>();
    std::size_t n = d.buttons.size();
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        auto rects = button_rects(d, node, *node.popup_area);
        for (std::size_t i = 0; i < rects.size(); ++i)
            if (rects[i].contains(mb->position)) return close(field, node, i);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        bool back = kp->key == keys::left || (kp->key == keys::tab && (kp->mods & mods::shift));
        bool forward = kp->key == keys::right || (kp->key == keys::tab && !(kp->mods & mods::shift));
        if (kp->key == keys::escape) close(field, node, std::nullopt);
        else if ((kp->key == keys::enter || kp->key == keys::space) && es.highlighted < n) close(field, node, es.highlighted);
        else if (n > 0 && forward) es.highlighted = (es.highlighted + 1) % n;
        else if (n > 0 && back) es.highlighted = (es.highlighted + n - 1) % n;
        node.dirty = true;
    }
}

} // namespace prism::ui
