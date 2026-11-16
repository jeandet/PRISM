#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

// value(x): what a Field, Derived, State or Shared holds, unwrapped from its widget.
//
//   core::Field<ui::Slider<>> volume;     value(volume)  -> the number
//   core::Field<ui::TextField<>> name;    value(name)    -> the text
//   core::Field<ui::Checkbox> mute;       value(mute)    -> the tick
//   core::Field<int> count;               value(count)   -> the int
//
// Found by argument-dependent lookup, so `value(volume)` needs no qualification. It reads through
// get(), so a Derived computing with it follows what it reads.
namespace prism::core {

template <typename T>
concept Readable = requires(const T& o) { o.get(); };

// A widget's main member: `.value` (slider, spin box, text, label, dropdown), else `.checked`
// (a toggle button's optional reads as off when unset), else the value itself.
template <typename W>
decltype(auto) widget_value(const W& w) {
    if constexpr (requires { w.value; }) return (w.value);
    else if constexpr (requires { w.checked.value_or(false); }) return w.checked.value_or(false);
    else if constexpr (requires { w.checked; }) return (w.checked);
    else return (w);
}

template <Readable O>
decltype(auto) value(const O& observable) {
    if constexpr (std::is_reference_v<decltype(observable.get())>)
        return widget_value(observable.get());
    else   // get() returned a copy (Shared): return a copy too, not a reference into it
        return std::remove_cvref_t<decltype(widget_value(observable.get()))>(
            widget_value(observable.get()));
}

// The member value() reads, writable: what set_value() assigns.
template <typename W>
auto& widget_value_ref(W& w) {
    if constexpr (requires { w.value; }) return w.value;
    else if constexpr (requires { w.checked; }) return w.checked;
    else return w;
}

// set_value(x, v): changes what value(x) reads and keeps the widget's other members (a slider
// keeps its range and label, a text field its placeholder). Only on what can be set: a Derived
// is computed, and does not compile here.
// The member set_value() writes, for an observable of type O.
template <typename O>
using value_member_t = std::remove_cvref_t<decltype(widget_value_ref(
    std::declval<std::remove_cvref_t<decltype(std::declval<O&>().get())>&>()))>;

// Converts, not merely assigns: a std::string takes an int as one character, which is never meant.
template <typename O, typename V>
concept SettableTo = requires(O& o) { o.update([](auto&) {}); }
                     && std::convertible_to<V, value_member_t<O>>;

template <typename O, typename V>
    requires SettableTo<O, V>
void set_value(O& observable, V&& new_value) {
    observable.update([&](auto& w) { widget_value_ref(w) = std::forward<V>(new_value); });
}

} // namespace prism::core

namespace prism {
using core::set_value;
using core::value;
} // namespace prism
