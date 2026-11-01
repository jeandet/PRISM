#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace prism::ui {

// What a widget is, for assistive technologies. The values are AccessKit's Role discriminants
// (accesskit 0.25, ARIA-derived), so a bridge converts with a static_cast.
enum class Role : std::uint8_t {
    Cell = 2, Label = 3, Row = 6, TreeItem = 9, GenericContainer = 14, CheckBox = 15,
    TextInput = 17, Button = 18, Pane = 20, ColumnHeader = 22, List = 24, Table = 25,
    MultilineTextInput = 31, PasswordInput = 40, Canvas = 51, ComboBox = 56, Group = 78,
    ScrollBar = 107, ScrollView = 108, Slider = 113, Tab = 120, TabList = 121, Tree = 129,
    Window = 133,
};

// What a widget says about itself: role, a human-readable name, and its value/state.
struct AccessInfo {
    Role role = Role::GenericContainer;
    std::string name;    // empty: the tree falls back to the field's name
    std::string value;   // text content or formatted value; never a password
    std::optional<double> numeric_value, min_value, max_value, step;
    std::optional<bool> checked, selected, expanded;
};

} // namespace prism::ui
