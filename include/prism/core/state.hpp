#pragma once

#include <prism/core/field.hpp>

namespace prism::core {

template <typename T>
struct State : ObservableValue<T, State<T>> {
    using ObservableValue<T, State<T>>::ObservableValue;
    using ObservableValue<T, State<T>>::operator=;
};

} // namespace prism::core
