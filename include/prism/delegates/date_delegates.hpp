#pragma once

#include <prism/ui/delegate.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prism::ui {

namespace date_detail {

using std::chrono::days;
using std::chrono::months;
using std::chrono::sys_days;
using std::chrono::year_month;
using std::chrono::year_month_day;

// Defined in src/delegates/date_delegates.cpp.
std::string to_iso(year_month_day d);

// "YYYY-MM-DD", a real date.
std::optional<year_month_day> parse_iso(std::string_view s);

inline year_month_day add_days(year_month_day d, int n) { return year_month_day{sys_days{d} + days{n}}; }

// The same day `n` months later, or that month's last day when it is shorter.
year_month_day add_months(year_month_day d, int n);

// The first cell of a month's grid: the Monday on or before its first day.
year_month_day grid_start(year_month ym);

std::string month_title(year_month ym);

// The calendar's geometry, in the widget's own coordinates: a header (‹ title ›), the weekday
// names, then six weeks of days.
constexpr float box_h = 30.f, pad = 8.f, cell_w = 28.f, cell_h = 24.f, header_h = 28.f, weekdays_h = 20.f;
constexpr float popup_w = pad + 7 * cell_w + pad;
constexpr float popup_h = pad + header_h + weekdays_h + 6 * cell_h + pad;

inline Rect prev_rect(Point popup) {
    return Rect{Point{popup.x + DX{pad}, popup.y + DY{pad}}, Size{Width{cell_w}, Height{header_h}}};
}
inline Rect next_rect(Point popup) {
    return Rect{Point{popup.x + DX{popup_w - pad - cell_w}, popup.y + DY{pad}}, Size{Width{cell_w}, Height{header_h}}};
}
Rect cell_rect(Point popup, int index);

// Which of the 42 cells holds `at`, if any.
std::optional<int> cell_at(Point popup, Point at);


void draw_box(DrawList& dl, year_month_day d, const WidgetNode& node, const Theme& t);

void draw_day(DrawList& dl, Rect cell, year_month_day d, bool picked, bool in_month, const Theme& t);

// simplify: no mark on today's date: that needs the local time zone, which the web build lacks.
void draw_calendar(DrawList& dl, DateEditState& es, year_month_day picked, Point top, const Theme& t);

void pick(DateEditState& es, Field<year_month_day>& field, year_month_day d);

// A field holding no real date (default-made: year 0, month 0) opens on this one's month.
constexpr year_month_day fallback{std::chrono::year{1970}, std::chrono::January, std::chrono::day{1}};

void open(DateEditState& es, year_month_day d, WidgetNode& node);

void close(DateEditState& es, WidgetNode& node);

void page(DateEditState& es, int by, WidgetNode& node);

void press(DateEditState& es, Point at, Field<year_month_day>& field, WidgetNode& node);

void key(DateEditState& es, int32_t k, Field<year_month_day>& field, WidgetNode& node);

} // namespace date_detail

inline void Widget<std::chrono::year_month_day>::describe(const Field<std::chrono::year_month_day>& f, AccessInfo& a) {
    a.value = date_detail::to_iso(f.get());
}

} // namespace prism::ui
