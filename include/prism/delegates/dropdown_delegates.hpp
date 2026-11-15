#pragma once

#include <prism/ui/delegate.hpp>

#include <cstddef>
#include <functional>
#include <string>

namespace prism::ui {

namespace dropdown_detail {

struct DropdownLabelResolver {
    std::function<std::string(size_t)> label_at;
    size_t count;
};

constexpr Height dd_widget_h{30.f};
constexpr Width dd_padding{8.f}; // used only as a horizontal inset below
constexpr float dd_font_size = 14.f;
constexpr Height dd_option_h{28.f};

// Defined in src/delegates/dropdown_delegates.cpp.
void dropdown_record(DrawList& dl, WidgetNode& node,
                     const std::string& current_label,
                     const DropdownLabelResolver& resolver);

bool dropdown_handle_input(const InputEvent& ev, WidgetNode& node,
                           size_t current_index, size_t count,
                           std::function<void(size_t)> select);

} // namespace dropdown_detail

// --- Widget<ScopedEnum T> method bodies ---

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Widget<T>::record(DrawList& dl, const Field<T>& field, WidgetNode& node) {
    size_t idx = enum_index(field.get());
    std::string label = enum_label<T>(idx);
    dropdown_detail::DropdownLabelResolver resolver{
        .label_at = [](size_t i) { return enum_label<T>(i); },
        .count = enum_count<T>()
    };
    dropdown_detail::dropdown_record(dl, node, label, resolver);
}

template <ScopedEnum T>
    requires (!TextEditable<T>)
void Widget<T>::handle_input(Field<T>& field, const InputEvent& ev, WidgetNode& node) {
    size_t current = enum_index(field.get());
    constexpr size_t count = enum_count<T>();
    dropdown_detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) { field.set(enum_from_index<T>(idx)); });
}

// --- Widget<Dropdown<T>> method bodies ---

template <ScopedEnum T>
void Widget<Dropdown<T>>::record(DrawList& dl, const Field<Dropdown<T>>& field,
                                    WidgetNode& node) {
    auto& dd = field.get();
    size_t idx = enum_index(dd.value);
    bool has_custom = !dd.labels.empty();
    std::string label = has_custom ? dd.labels[idx] : enum_label<T>(idx);
    dropdown_detail::DropdownLabelResolver resolver{
        .label_at = [&dd, has_custom](size_t i) -> std::string {
            return has_custom ? dd.labels[i] : enum_label<T>(i);
        },
        .count = enum_count<T>()
    };
    dropdown_detail::dropdown_record(dl, node, label, resolver);
}

template <ScopedEnum T>
void Widget<Dropdown<T>>::handle_input(Field<Dropdown<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    auto dd = field.get();
    size_t current = enum_index(dd.value);
    constexpr size_t count = enum_count<T>();
    dropdown_detail::dropdown_handle_input(ev, node, current, count,
        [&](size_t idx) {
            dd.value = enum_from_index<T>(idx);
            field.set(dd);
        });
}

} // namespace prism::ui
