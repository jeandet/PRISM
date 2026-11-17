#pragma once

#include <prism/ui/delegate.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace prism::ui {

namespace tabs_detail {

constexpr Height tab_h{32.f};
constexpr Width tab_padding{16.f};
constexpr float tab_font_size = 14.f;
constexpr Height tab_accent_h{2.f};

// Defined in src/delegates/tabs_delegates.cpp.
void tabs_record(DrawList& dl, WidgetNode& node,
                 size_t selected, const std::vector<std::string>& names);

// The tab under x (in the bar's coordinates), as last drawn.
std::optional<size_t> tab_at(const WidgetNode& bar, X x);

// Where a tab dropped at x goes: before the tab whose left half holds x, or after the last.
size_t insertion_at(const WidgetNode& bar, X x);
// The line drawn there while a tab is dragged over the bar.
Rect insertion_marker(const WidgetNode& bar, size_t index);

bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                       size_t selected, size_t count,
                       std::function<void(size_t)> select);

} // namespace tabs_detail

} // namespace prism::ui
