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

bool tabs_handle_input(const InputEvent& ev, WidgetNode& node,
                       size_t selected, size_t count,
                       std::function<void(size_t)> select);

} // namespace tabs_detail

} // namespace prism::ui
