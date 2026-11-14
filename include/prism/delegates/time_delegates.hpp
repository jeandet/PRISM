#pragma once

#include <prism/ui/widget_node.hpp>

#include <cctype>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace prism::ui {

namespace time_detail {

using Part = TimeEditState::Part;

inline std::string to_text(TimeOfDay t) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", t.hour, t.minute);
    return buf;
}

// "HH:MM", a real time of day.
inline std::optional<TimeOfDay> parse(std::string_view s) {
    if (s.size() != 5 || s[2] != ':') return std::nullopt;
    auto number = [&](std::size_t at) -> std::optional<int> {
        if (!std::isdigit(static_cast<unsigned char>(s[at])) || !std::isdigit(static_cast<unsigned char>(s[at + 1])))
            return std::nullopt;   // from_chars would take a sign
        int v = 0;
        auto [end, err] = std::from_chars(s.data() + at, s.data() + at + 2, v);
        if (err != std::errc{} || end != s.data() + at + 2) return std::nullopt;
        return v;
    };
    auto h = number(0), m = number(3);
    if (!h || !m || *h > 23 || *m > 59) return std::nullopt;
    return TimeOfDay{*h, *m};
}

inline int limit(Part p) { return p == Part::hour ? 24 : 60; }
inline int& value(TimeOfDay& t, Part p) { return p == Part::hour ? t.hour : t.minute; }

// One part moved by `by`, wrapping within it: the other part stays.
inline TimeOfDay step(TimeOfDay t, Part p, int by) {
    int& v = value(t, p);
    v = ((v + by) % limit(p) + limit(p)) % limit(p);
    return t;
}

// A digit typed into the current part. A first digit that a second one could still complete
// waits as `pending`; a part once complete hands the keys to the minutes.
inline TimeOfDay type_digit(TimeOfDay t, TimeEditState& es, int digit) {
    int max = limit(es.part) - 1;
    if (es.pending && *es.pending * 10 + digit <= max) {
        value(t, es.part) = *es.pending * 10 + digit;
        es.pending.reset();
        es.part = Part::minute;
        return t;
    }
    value(t, es.part) = digit;
    if (digit * 10 <= max) es.pending = digit;
    else es.pending.reset(), es.part = Part::minute;
    return t;
}

constexpr float box_h = 30.f, text_x = 8.f, font = 14.f;

// Where the hour ends and the minutes start, measured in the theme's font.
inline float minutes_x(const WidgetNode& node) {
    auto* m = node_theme(node).measurer;
    return text_x + text_width(m, "00", font) + text_width(m, ":", font);
}

inline void draw(DrawList& dl, TimeOfDay t, const TimeEditState& es, const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& th = node_theme(node);
    Width w = delegate_detail::widget_w(node);
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{box_h}), vs.hovered ? th.surface_hover : th.surface);
    if (vs.focused) {
        float two = text_width(th.measurer, "00", font);
        float x = es.part == Part::hour ? text_x : minutes_x(node);
        dl.filled_rect(delegate_detail::make_rect(X{x - 1}, Y{5}, Width{two + 2}, Height{20}), th.text_selection);
    }
    dl.text(to_text(t), delegate_detail::make_point(X{text_x}, Y{7}), font, th.text);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, Height{box_h}, th);
}

inline void on_key(TimeEditState& es, int32_t k, Field<TimeOfDay>& field, WidgetNode& node) {
    switch (k) {
    case keys::up: field.set(step(field.get(), es.part, 1)); break;
    case keys::down: field.set(step(field.get(), es.part, -1)); break;
    case keys::left: es.part = Part::hour; break;
    case keys::right: es.part = Part::minute; break;
    default: return;
    }
    es.pending.reset();
    node.dirty = true;
}

inline void on_text(TimeEditState& es, std::string_view text, Field<TimeOfDay>& field, WidgetNode& node) {
    for (char c : text)
        if (c >= '0' && c <= '9') field.set(type_digit(field.get(), es, c - '0'));
    node.dirty = true;
}

} // namespace time_detail

inline void Widget<TimeOfDay>::describe(const Field<TimeOfDay>& f, AccessInfo& a) { a.value = time_detail::to_text(f.get()); }

inline void Widget<TimeOfDay>::record(DrawList& dl, const Field<TimeOfDay>& field, WidgetNode& node) {
    auto& es = node.get_or_create<TimeEditState>();
    if (!node_vs(node).focused) es.pending.reset();   // a half-typed part ends with the focus
    time_detail::draw(dl, field.get(), es, node);
}

inline void Widget<TimeOfDay>::handle_input(Field<TimeOfDay>& field, const InputEvent& ev, WidgetNode& node) {
    using time_detail::Part;
    auto& es = node.get_or_create<TimeEditState>();
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        es.part = mb->position.x.raw() < time_detail::minutes_x(node) - 2.f ? Part::hour : Part::minute;
        es.pending.reset();
        node.dirty = true;
    } else if (auto* ms = std::get_if<MouseScroll>(&ev); ms && ms->dy.raw() != 0) {
        es.pending.reset();
        field.set(time_detail::step(field.get(), es.part, ms->dy.raw() > 0 ? 1 : -1));   // dy > 0: away from the user
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        time_detail::on_key(es, kp->key, field, node);
    } else if (auto* ti = std::get_if<TextInput>(&ev)) {
        time_detail::on_text(es, ti->text, field, node);
    } else if (auto* p = std::get_if<Paste>(&ev)) {
        time_detail::on_text(es, p->text, field, node);   // "12:45": its digits, typed
    }
}

} // namespace prism::ui
