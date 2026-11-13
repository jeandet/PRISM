#pragma once

#include <cstdint>

namespace prism::ui {

enum class CursorShape : uint8_t { Default, Text, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE, NotAllowed, Pointer };

} // namespace prism::ui
