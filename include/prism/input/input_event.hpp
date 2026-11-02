#pragma once

#include <prism/core/types.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace prism::input {
using namespace prism::core;


struct MouseMove   { Point position; uint32_t buttons = 0; };   // buttons: held, as a bitmask
struct MouseButton { Point position; uint8_t button; bool pressed; uint16_t mods = 0; uint8_t clicks = 1; };
struct MouseScroll { Point position; DX dx; DY dy; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct TextInput   { std::string text; };
struct Paste       { std::string text; };   // clipboard contents, read by the backend on Ctrl/Cmd+V
// Input-method preedit (e.g. pinyin before it becomes 你): shown at the caret, not yet text.
// start/length locate the IME's caret/selection inside it; -1 when the IME gives none.
struct Composition { std::string text; int32_t start = -1; int32_t length = -1; };
struct WindowResize { int width, height; };
struct WindowClose {};
struct MouseLeave {};   // the pointer left the window's content (or went onto its title bar)

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput, Paste, Composition,
    WindowResize, WindowClose, MouseLeave
>;

namespace keys {
    inline constexpr int32_t tab       = 0x09;         // matches SDLK_TAB
    inline constexpr int32_t space     = 0x20;         // matches SDLK_SPACE
    inline constexpr int32_t enter     = 0x0D;         // matches SDLK_RETURN
    inline constexpr int32_t backspace = 0x08;         // matches SDLK_BACKSPACE
    inline constexpr int32_t delete_   = 0x7F;         // matches SDLK_DELETE
    inline constexpr int32_t right     = 0x4000'004F;  // matches SDLK_RIGHT
    inline constexpr int32_t left      = 0x4000'0050;  // matches SDLK_LEFT
    inline constexpr int32_t home      = 0x4000'004A;  // matches SDLK_HOME
    inline constexpr int32_t end       = 0x4000'004D;  // matches SDLK_END
    inline constexpr int32_t up        = 0x4000'0052;  // matches SDLK_UP
    inline constexpr int32_t down      = 0x4000'0051;  // matches SDLK_DOWN
    inline constexpr int32_t escape    = 0x1B;          // matches SDLK_ESCAPE
    inline constexpr int32_t page_up   = 0x4000'004B;  // matches SDLK_PAGEUP
    inline constexpr int32_t page_down = 0x4000'004E;  // matches SDLK_PAGEDOWN
    inline constexpr int32_t a         = 0x61;         // matches SDLK_A
    inline constexpr int32_t c         = 0x63;         // matches SDLK_C
    inline constexpr int32_t i         = 0x69;         // matches SDLK_I
    inline constexpr int32_t m         = 0x6D;         // matches SDLK_M
    inline constexpr int32_t v         = 0x76;         // matches SDLK_V
    inline constexpr int32_t x         = 0x78;         // matches SDLK_X
    inline constexpr int32_t y         = 0x79;         // matches SDLK_Y
    inline constexpr int32_t z         = 0x7A;         // matches SDLK_Z
}

namespace mods {
    inline constexpr uint16_t shift = 0x0003;  // matches SDL_KMOD_SHIFT
    inline constexpr uint16_t ctrl  = 0x00C0;  // matches SDL_KMOD_CTRL
    inline constexpr uint16_t alt   = 0x0300;  // matches SDL_KMOD_ALT
    inline constexpr uint16_t gui   = 0x0C00;  // matches SDL_KMOD_GUI (Cmd on macOS)
    // Shortcut modifier: Ctrl, or Cmd on macOS. Not with Alt: AltGr arrives as Ctrl+Alt, and
    // AltGr+X is a character (e.g. Polish 'ź'), not cut.
    inline constexpr bool shortcut(uint16_t m) { return (m & (ctrl | gui)) != 0 && (m & alt) == 0; }
}

namespace buttons {
    inline constexpr uint8_t left   = 1;  // matches SDL_BUTTON_LEFT
    inline constexpr uint8_t middle = 2;  // matches SDL_BUTTON_MIDDLE
    inline constexpr uint8_t right  = 3;  // matches SDL_BUTTON_RIGHT
    inline constexpr uint32_t left_mask = 1u << (left - 1);   // matches SDL_BUTTON_LMASK
}

inline InputEvent localize_mouse(const InputEvent& ev, Rect widget_rect) {
    Offset off{DX{widget_rect.origin.x.raw()}, DY{widget_rect.origin.y.raw()}};
    return std::visit([&](auto e) -> InputEvent {
        if constexpr (requires { e.position; })
            e.position = e.position - off;
        return e;
    }, ev);
}

} // namespace prism::input
