#pragma once

#include <prism/core/types.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace prism::input {
using namespace prism::core;


struct MouseMove   { Point position; uint32_t buttons = 0; };   // buttons: held, as a bitmask
struct MouseButton { Point position; uint8_t button; bool pressed; uint16_t mods = 0; uint8_t clicks = 1; };
// The wheel, with SDL's signs: dy > 0 turned away from the user (up), dx > 0 to the right.
// mods: the keys held as it turned (a browser reports a trackpad pinch as Ctrl+wheel).
struct MouseScroll { Point position; DX dx; DY dy; uint16_t mods = 0; };
struct KeyPress    { int32_t key; uint16_t mods; };
struct KeyRelease  { int32_t key; uint16_t mods; };
struct TextInput   { std::string text; };
struct Paste       { std::string text; };   // clipboard contents, read by the backend on Ctrl/Cmd+V
// Input-method preedit (e.g. pinyin before it becomes 你): shown at the caret, not yet text.
// start/length locate the IME's caret/selection inside it; -1 when the IME gives none.
struct Composition { std::string text; int32_t start = -1; int32_t length = -1; };
// A window's new size, or its new place on the desktop (where the platform tells: not Wayland).
// `maximized`: it fills the screen, and the size or place to come back to is the one before.
struct WindowResize { int width, height; bool maximized = false; };
struct WindowMove { int x, y; bool maximized = false; };
struct WindowClose {};
struct MouseLeave {};   // the pointer left the window's content (or went onto its title bar)
// Dropped from another app: the files of one drop together, or text. Where the pointer was.
struct FileDrop { Point position; std::vector<std::string> paths; };
struct TextDrop { Point position; std::string text; };
// The OS's light or dark preference: sent once at start, then whenever the user changes it.
struct SystemTheme { bool dark = true; };

using InputEvent = std::variant<
    MouseMove, MouseButton, MouseScroll,
    KeyPress, KeyRelease, TextInput, Paste, Composition,
    WindowResize, WindowMove, WindowClose, MouseLeave, FileDrop, TextDrop, SystemTheme
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
    inline constexpr int32_t f1        = 0x4000'003A;  // matches SDLK_F1; F2..F12 follow
    inline constexpr int32_t f2        = f1 + 1;
    inline constexpr int32_t f5        = f1 + 4;
    inline constexpr int32_t f12       = f1 + 11;
    inline constexpr int32_t a         = 0x61;         // matches SDLK_A
    inline constexpr int32_t b         = 0x62;         // matches SDLK_B
    inline constexpr int32_t c         = 0x63;         // matches SDLK_C
    inline constexpr int32_t e         = 0x65;         // matches SDLK_E
    inline constexpr int32_t f         = 0x66;         // matches SDLK_F
    inline constexpr int32_t i         = 0x69;         // matches SDLK_I
    inline constexpr int32_t k         = 0x6B;         // matches SDLK_K
    inline constexpr int32_t m         = 0x6D;         // matches SDLK_M
    inline constexpr int32_t o         = 0x6F;         // matches SDLK_O
    inline constexpr int32_t s         = 0x73;         // matches SDLK_S
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

// Where a widget is drawn, and at what zoom (a zoomed board's cards): its own coordinates
// start at rect's origin, scaled by `scale`. Every conversion between the window and a widget's
// own coordinates goes through here.
struct Placed {
    Rect rect;
    float scale = 1.f;

    [[nodiscard]] Point to_local(Point window) const { return Point{} + (window - rect.origin) / scale; }
    [[nodiscard]] Rect to_local(Rect window) const { return {to_local(window.origin), window.extent / scale}; }
    [[nodiscard]] Point to_window(Point local) const { return rect.origin + (local - Point{}) * scale; }
    [[nodiscard]] Rect to_window(Rect local) const { return {to_window(local.origin), local.extent * scale}; }
};

inline InputEvent localize_mouse(const InputEvent& ev, const Placed& at) {
    return std::visit([&](auto e) -> InputEvent {
        if constexpr (requires { e.position; }) e.position = at.to_local(e.position);
        return e;
    }, ev);
}

} // namespace prism::input
