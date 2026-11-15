#pragma once

#include <prism/ui/delegate.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prism::ui {

namespace time_detail {

using Part = TimeEditState::Part;

// Defined in src/delegates/time_delegates.cpp.
std::string to_text(TimeOfDay t);

// "HH:MM", a real time of day.
std::optional<TimeOfDay> parse(std::string_view s);

inline int limit(Part p) { return p == Part::hour ? 24 : 60; }
inline int& value(TimeOfDay& t, Part p) { return p == Part::hour ? t.hour : t.minute; }

// One part moved by `by`, wrapping within it: the other part stays.
TimeOfDay step(TimeOfDay t, Part p, int by);

// A digit typed into the current part. A first digit that a second one could still complete
// waits as `pending`; a part once complete hands the keys to the minutes.
TimeOfDay type_digit(TimeOfDay t, TimeEditState& es, int digit);

constexpr float box_h = 30.f, text_x = 8.f, font = 14.f;

// Where the hour ends and the minutes start, measured in the theme's font.
float minutes_x(const WidgetNode& node);

void draw(DrawList& dl, TimeOfDay t, const TimeEditState& es, const WidgetNode& node);

void on_key(TimeEditState& es, int32_t k, Field<TimeOfDay>& field, WidgetNode& node);

void on_text(TimeEditState& es, std::string_view text, Field<TimeOfDay>& field, WidgetNode& node);

} // namespace time_detail

inline void Widget<TimeOfDay>::describe(const Field<TimeOfDay>& f, AccessInfo& a) { a.value = time_detail::to_text(f.get()); }

} // namespace prism::ui
