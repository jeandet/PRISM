#include <prism/delegates/date_delegates.hpp>

#include <prism/ui/popup_place.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace prism::ui {

namespace date_detail {

std::string to_iso(year_month_day d) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(d.year()), static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

std::optional<year_month_day> parse_iso(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
    auto number = [&](std::size_t at, std::size_t len) -> std::optional<int> {
        for (std::size_t i = at; i < at + len; ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;   // from_chars would take a sign
        int v = 0;
        auto [end, err] = std::from_chars(s.data() + at, s.data() + at + len, v);
        if (err != std::errc{} || end != s.data() + at + len) return std::nullopt;
        return v;
    };
    auto y = number(0, 4), m = number(5, 2), d = number(8, 2);
    if (!y || !m || !d) return std::nullopt;
    year_month_day out{std::chrono::year{*y}, std::chrono::month{static_cast<unsigned>(*m)},
                       std::chrono::day{static_cast<unsigned>(*d)}};
    return out.ok() ? std::optional{out} : std::nullopt;
}

year_month_day add_months(year_month_day d, int n) {
    year_month ym = d.year() / d.month() + months{n};
    auto last = (ym / std::chrono::last).day();
    return ym / std::min(d.day(), last);
}

year_month_day grid_start(year_month ym) {
    sys_days first{ym / 1};
    unsigned since_monday = (std::chrono::weekday{first}.c_encoding() + 6) % 7;
    return year_month_day{first - days{since_monday}};
}

std::string month_title(year_month ym) {
    static constexpr std::array<const char*, 12> names{"January", "February", "March",     "April",   "May",      "June",
                                                       "July",    "August",   "September", "October", "November", "December"};
    return std::string(names[static_cast<unsigned>(ym.month()) - 1]) + " " + std::to_string(static_cast<int>(ym.year()));
}

Rect cell_rect(Point popup, int index) {
    return Rect{Point{popup.x + DX{pad + static_cast<float>(index % 7) * cell_w},
                      popup.y + DY{pad + header_h + weekdays_h + static_cast<float>(index / 7) * cell_h}},
                Size{Width{cell_w}, Height{cell_h}}};
}

std::optional<int> cell_at(Point popup, Point at) {
    for (int i = 0; i < 42; ++i)
        if (cell_rect(popup, i).contains(at)) return i;
    return std::nullopt;
}


void draw_box(DrawList& dl, year_month_day d, const WidgetNode& node, const Theme& t) {
    auto& vs = node_vs(node);
    Width w = delegate_detail::widget_w(node);
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{box_h}), vs.hovered ? t.surface_hover : t.surface);
    dl.text(to_iso(d), delegate_detail::make_point(X{8}, Y{7}), 14, t.text);
    dl.text("\xe2\x96\xbe", delegate_detail::make_point(X{w.raw() - 20.f}, Y{7}), 14, t.text_muted);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, Height{box_h}, t);
}

void draw_day(DrawList& dl, Rect cell, year_month_day d, bool picked, bool in_month, const Theme& t) {
    if (picked) dl.filled_rect(cell, t.primary);
    unsigned n = static_cast<unsigned>(d.day());
    Color ink = picked ? t.text_on_primary : in_month ? t.text : t.text_placeholder;
    dl.text(std::to_string(n), Point{cell.origin.x + DX{n < 10 ? 10.f : 6.f}, cell.origin.y + DY{4}}, 13, ink);
}

void draw_calendar(DrawList& dl, DateEditState& es, year_month_day picked, Point top, const Theme& t) {
    Rect area{top, Size{Width{popup_w}, Height{popup_h}}};
    dl.filled_rect(area, t.popup_bg);
    dl.rect_outline(area, t.popup_border, 1.f);
    dl.text("\xe2\x80\xb9", prev_rect(top).origin + Offset{DX{10}, DY{6}}, 14, t.text);   // ‹
    dl.text("\xe2\x80\xba", next_rect(top).origin + Offset{DX{10}, DY{6}}, 14, t.text);   // ›
    dl.text(month_title(es.shown), top + Offset{DX{pad + cell_w + 12.f}, DY{pad + 6}}, 14, t.text);
    static constexpr std::array<const char*, 7> weekdays{"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (int c = 0; c < 7; ++c)
        dl.text(weekdays[static_cast<std::size_t>(c)],
                top + Offset{DX{pad + static_cast<float>(c) * cell_w + 6.f}, DY{pad + header_h + 2}}, 12, t.text_muted);
    auto first = grid_start(es.shown);
    for (int i = 0; i < 42; ++i) {
        auto d = add_days(first, i);
        draw_day(dl, cell_rect(top, i), d, d == picked, d.year() / d.month() == es.shown, t);
    }
    es.popup_rect = area;
}

void pick(DateEditState& es, Field<year_month_day>& field, year_month_day d) {
    es.shown = d.year() / d.month();
    field.set(d);
}

void open(DateEditState& es, year_month_day d, WidgetNode& node) {
    if (!d.ok()) d = fallback;
    es.open = true;
    es.shown = d.year() / d.month();
    node.dirty = true;
}

void close(DateEditState& es, WidgetNode& node) {
    es.open = false;
    node.dirty = true;
}

void page(DateEditState& es, int by, WidgetNode& node) {
    es.shown += months{by};
    node.dirty = true;
}

void press(DateEditState& es, Point at, Field<year_month_day>& field, WidgetNode& node) {
    if (!es.open) return open(es, field.get(), node);
    Point top = es.popup_rect.origin;
    if (prev_rect(top).contains(at)) return page(es, -1, node);
    if (next_rect(top).contains(at)) return page(es, 1, node);
    if (auto i = cell_at(top, at)) {
        pick(es, field, add_days(grid_start(es.shown), *i));
        return close(es, node);
    }
    if (!es.popup_rect.contains(at)) close(es, node);   // the box itself: a second click closes
}

void key(DateEditState& es, int32_t k, Field<year_month_day>& field, WidgetNode& node) {
    if (!es.open) {
        if (k == keys::space || k == keys::enter) open(es, field.get(), node);
        return;
    }
    auto d = field.get().ok() ? field.get() : fallback;
    switch (k) {
    case keys::escape:
    case keys::enter: close(es, node); break;
    case keys::left: pick(es, field, add_days(d, -1)); break;
    case keys::right: pick(es, field, add_days(d, 1)); break;
    case keys::up: pick(es, field, add_days(d, -7)); break;
    case keys::down: pick(es, field, add_days(d, 7)); break;
    case keys::page_up: pick(es, field, add_months(d, -1)); break;
    case keys::page_down: pick(es, field, add_months(d, 1)); break;
    default: break;
    }
}

} // namespace date_detail

void Widget<std::chrono::year_month_day>::record(DrawList& dl, const Field<std::chrono::year_month_day>& field,
                                                          WidgetNode& node) {
    auto& es = node.get_or_create<DateEditState>();
    auto& t = node_theme(node);
    date_detail::draw_box(dl, field.get(), node, t);
    node.overlay_draws.clear();
    if (es.open)
        date_detail::draw_calendar(node.overlay_draws, es, field.get(),
                                   popup_origin(node, Size{Width{date_detail::popup_w}, Height{date_detail::popup_h}}, date_detail::box_h), t);
}

void Widget<std::chrono::year_month_day>::handle_input(Field<std::chrono::year_month_day>& field,
                                                                const InputEvent& ev, WidgetNode& node) {
    auto& es = node.get_or_create<DateEditState>();
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left)
        date_detail::press(es, mb->position, field, node);
    else if (auto* kp = std::get_if<KeyPress>(&ev))
        date_detail::key(es, kp->key, field, node);
}

} // namespace prism::ui
