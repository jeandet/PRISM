#pragma once

#include <type_traits>

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

} // namespace prism::core

namespace prism {
using core::value;
} // namespace prism
