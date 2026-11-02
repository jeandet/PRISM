#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <string_view>
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
constexpr float title_h = 30.f;
constexpr float line_h = 20.f;

constexpr float max_text_w = 560.f;   // a long message wraps here, or at the window if narrower
constexpr float fixed_h = pad + title_h + 10.f + button_h + pad;   // everything but the message

// The window in the anchor's own coordinates.
inline Rect window_rect(const WidgetNode& node) {
    return Rect{Point{X{-node.absolute_x.raw()}, Y{-node.absolute_y.raw()}},
                Size{node.viewport_width, node.viewport_height}};
}

inline float button_w(const Dialog& d, const WidgetNode& node, std::size_t i) {
    return std::max(72.f, text_width(node_theme(node).measurer, d.buttons[i], text_size) + 2.f * button_pad);
}

// Where everything goes, from the dialog and the window: drawing and input both read it.
struct Layout {
    Rect card;
    std::vector<LineSpan> lines;   // the message, wrapped
    float message_h = 0.f;         // shown: the whole message, or what the window leaves
    float max_scroll = 0.f;
    Rect message_area() const {
        return Rect{Point{card.origin.x + DX{pad}, card.origin.y + DY{pad + title_h}},
                    Size{Width{card.extent.w.raw() - 2.f * pad}, Height{message_h}}};
    }
};

// A wrapped line without the spaces hanging off its end (they are not drawn or measured).
inline std::string_view shown_line(std::string_view text, LineSpan span) {
    auto line = text.substr(span.start, span.length);
    while (!line.empty() && is_wrap_space(line.back())) line.remove_suffix(1);
    return line;
}

// Centered, never past the top-left corner when the window is smaller than the card (the same
// place the popup layer would slide it to, so the two never fight), and no larger than the
// window: a long message wraps and scrolls instead of pushing the buttons out of reach.
inline Layout layout(const Dialog& d, const WidgetNode& node) {
    const auto* m = node_theme(node).measurer;
    Rect win = window_rect(node);
    float win_w = win.extent.w.raw() > 0.f ? win.extent.w.raw() : 1e6f;
    float win_h = win.extent.h.raw() > 0.f ? win.extent.h.raw() : 1e6f;
    Layout l;
    float wrap_w = std::max(80.f, std::min(max_text_w, win_w - 2.f * pad));
    l.lines = wrap_lines(d.message, Width{wrap_w}, m, text_size, FontRole::Ui);
    float buttons = 0.f;
    for (std::size_t i = 0; i < d.buttons.size(); ++i) buttons += button_w(d, node, i) + button_gap;
    float w = std::max({min_w, buttons + 2.f * pad, text_width(m, d.title, title_size) + 2.f * pad});
    for (auto span : l.lines) w = std::max(w, text_width(m, shown_line(d.message, span), text_size) + 2.f * pad);
    w = std::min(w, std::max(min_w, win_w));
    float total = line_h * static_cast<float>(l.lines.size());
    l.message_h = std::min(total, std::max(line_h, win_h - fixed_h));
    l.max_scroll = total - l.message_h;
    float h = fixed_h + l.message_h;
    float x = win.origin.x.raw() + std::max(0.f, (win.extent.w.raw() - w) / 2.f);
    float y = win.origin.y.raw() + std::max(0.f, (win.extent.h.raw() - h) / 2.f);
    l.card = Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}};
    return l;
}

inline Rect card_rect(const Dialog& d, const WidgetNode& node) { return layout(d, node).card; }

// The message's scrollbar column at the card's right edge, and the thumb in it; none when the
// message fits.
inline std::optional<Rect> scroll_track(const Layout& l) {
    if (l.max_scroll <= 0.f) return std::nullopt;
    Rect area = l.message_area();
    return Rect{Point{l.card.origin.x + DX{l.card.extent.w.raw() - pad}, area.origin.y},
                Size{Width{pad}, area.extent.h}};
}
inline float thumb_h(const Layout& l) {
    float track = l.message_h;
    return std::max(20.f, track * track / (track + l.max_scroll));
}
inline Rect thumb_rect(const Layout& l, float scroll) {
    Rect track = *scroll_track(l);
    float ty = track.origin.y.raw() + (l.message_h - thumb_h(l)) * scroll / l.max_scroll;
    return Rect{Point{track.origin.x + DX{pad / 2.f - 3.f}, Y{ty}}, Size{Width{6.f}, Height{thumb_h(l)}}};
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

inline void draw(const Dialog& d, WidgetNode& node, const Layout& l) {
    const auto& t = *node.theme;
    const Rect& card = l.card;
    auto& dl = node.overlay_draws;
    auto& es = node.get_or_create<DialogEditState>();
    es.highlighted = std::min(es.highlighted, d.buttons.empty() ? 0 : d.buttons.size() - 1);
    es.scroll = std::clamp(es.scroll, 0.f, l.max_scroll);   // the window may have grown
    std::size_t hl = es.highlighted;
    dl.filled_rect(window_rect(node), t.scrim);
    dl.rounded_rect(card, t.popup_bg, 8.f);
    dl.rounded_rect(card, t.popup_border, 8.f, 1.f);
    dl.text(d.title, Point{card.origin.x + DX{pad}, card.origin.y + DY{pad}}, title_size, t.text);
    Rect area = l.message_area();
    dl.clip_push(area.origin, area.extent);
    for (std::size_t i = 0; i < l.lines.size(); ++i) {
        float y = area.origin.y.raw() + line_h * static_cast<float>(i) - es.scroll;
        if (y + line_h <= area.origin.y.raw() || y >= area.origin.y.raw() + area.extent.h.raw()) continue;
        dl.text(std::string(shown_line(d.message, l.lines[i])), Point{area.origin.x, Y{y}}, text_size, t.text);
    }
    dl.clip_pop();
    if (scroll_track(l)) dl.filled_rect(thumb_rect(l, es.scroll), t.scrollbar_thumb);
    auto rects = button_rects(d, node, card);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        dl.rounded_rect(rects[i], i == hl ? t.primary : t.surface, 4.f);
        float tw = text_width(node_theme(node).measurer, d.buttons[i], text_size);
        dl.text(d.buttons[i],
                Point{rects[i].origin.x + DX{(rects[i].extent.w.raw() - tw) / 2.f}, rects[i].origin.y + DY{7.f}},
                text_size, i == hl ? t.text_on_primary : t.text);
    }
}

// Scrolls a message taller than the dialog allows -- wheel, keys, or dragging its scrollbar;
// false when the event is none of those.
// simplify: the dialog paints and scrolls its own message. Upgrade: let a popup host a subtree
// (a ScrollArea body, a row of buttons), which brings wrapping, scrolling, dragging and
// accessibility for the message from the ordinary widgets.
inline bool scroll_message(DialogEditState& es, const Layout& l, const InputEvent& ev) {
    float to = es.scroll;
    auto track = scroll_track(l);
    auto* mb = std::get_if<MouseButton>(&ev);
    if (mb && mb->button == buttons::left && !mb->pressed && es.drag) {
        es.drag.reset();
        return true;
    }
    if (mb && mb->button == buttons::left && mb->pressed && track && track->contains(mb->position)) {
        es.drag = std::pair{mb->position.y.raw(), es.scroll};
        return true;
    }
    if (auto* mm = std::get_if<MouseMove>(&ev); mm && es.drag) {
        float range = l.message_h - thumb_h(l);
        if (range > 0.f) to = es.drag->second + (mm->position.y.raw() - es.drag->first) * l.max_scroll / range;
    } else if (auto* ms = std::get_if<MouseScroll>(&ev)) to += ms->dy.raw() * 3.f * line_h;
    else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::down) to += line_h;
        else if (kp->key == keys::up) to -= line_h;
        else if (kp->key == keys::page_down) to += l.message_h;
        else if (kp->key == keys::page_up) to -= l.message_h;
        else if (kp->key == keys::home) to = 0.f;
        else if (kp->key == keys::end) to = l.max_scroll;
        else return false;
    } else return false;
    es.scroll = std::clamp(to, 0.f, l.max_scroll);
    return true;
}

} // namespace dialog_detail

inline void Widget<Dialog>::record(DrawList&, const Field<Dialog>& field, WidgetNode& node) {
    using namespace dialog_detail;
    const auto& d = field.get();
    node.overlay_draws.clear();
    if (!d.open) {
        node.popup_area.reset();
        node.edit_state.reset();   // closed by the app: the next opening starts afresh, as after close()
        return;
    }
    auto l = layout(d, node);
    node.popup_area = l.card;
    node.popup_modal = true;
    draw(d, node, l);
}

inline void Widget<Dialog>::handle_input(Field<Dialog>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace dialog_detail;
    const auto& d = field.get();
    if (!d.open || !node.popup_area) return;
    auto& es = node.get_or_create<DialogEditState>();
    std::size_t n = d.buttons.size();
    es.highlighted = std::min(es.highlighted, n > 0 ? n - 1 : 0);   // buttons may have shrunk
    if (scroll_message(es, layout(d, node), ev)) {
        node.dirty = true;
        return;
    }
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
