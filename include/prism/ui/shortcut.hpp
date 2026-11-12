#pragma once

#include <prism/input/input_event.hpp>

#include <cstdint>
#include <string>

// Keyboard shortcuts: Ctrl+S on a menu item chooses it from anywhere in the window.
namespace prism::ui {

// A key and its modifiers. mods::ctrl means "Ctrl, or Cmd on macOS", as mods::shortcut does.
// No key: no shortcut.
struct Shortcut {
    std::int32_t key = 0;
    std::uint16_t mods = 0;
    bool operator==(const Shortcut&) const = default;
};

namespace shortcut_detail {
// Ctrl, Shift and Alt, each as one bit, whichever side was pressed; Cmd counts as Ctrl.
inline std::uint16_t normalized(std::uint16_t m) {
    std::uint16_t out = 0;
    if (m & (input::mods::ctrl | input::mods::gui)) out |= input::mods::ctrl;
    if (m & input::mods::shift) out |= input::mods::shift;
    if (m & input::mods::alt) out |= input::mods::alt;
    return out;
}

inline std::string key_name(std::int32_t key) {
    namespace k = input::keys;
    if ((key >= 'a' && key <= 'z')) return std::string(1, static_cast<char>(key - 'a' + 'A'));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    if (key >= k::f1 && key <= k::f12) return "F" + std::to_string(key - k::f1 + 1);
    switch (key) {
    case k::delete_: return "Delete";
    case k::backspace: return "Backspace";
    case k::enter: return "Enter";
    case k::escape: return "Esc";
    case k::tab: return "Tab";
    case k::space: return "Space";
    case k::home: return "Home";
    case k::end: return "End";
    case k::page_up: return "PgUp";
    case k::page_down: return "PgDown";
    case k::left: return "Left";
    case k::right: return "Right";
    case k::up: return "Up";
    case k::down: return "Down";
    default: return key > 32 && key < 127 ? std::string(1, static_cast<char>(key)) : "?";
    }
}
} // namespace shortcut_detail

inline bool matches(Shortcut s, const input::KeyPress& kp) {
    return s.key != 0 && kp.key == s.key
        && shortcut_detail::normalized(kp.mods) == shortcut_detail::normalized(s.mods);
}

// "Ctrl+Shift+S", as shown beside a menu item.
// simplify: always "Ctrl", also on macOS where the key is Cmd (⌘); the symbols come with a
// per-platform style.
inline std::string shortcut_text(Shortcut s) {
    if (s.key == 0) return {};
    auto m = shortcut_detail::normalized(s.mods);
    std::string out;
    if (m & input::mods::ctrl) out += "Ctrl+";
    if (m & input::mods::alt) out += "Alt+";
    if (m & input::mods::shift) out += "Shift+";
    return out + shortcut_detail::key_name(s.key);
}

// The keys a focused text widget keeps for itself, even when a menu item has the same
// shortcut: typing (anything without Ctrl), and copy, cut, paste, undo, redo, select all,
// moving or deleting by word. Function keys still reach the menus.
inline bool text_widget_keeps(const input::KeyPress& kp) {
    namespace k = input::keys;
    if (kp.key >= k::f1 && kp.key <= k::f12) return false;
    if (!input::mods::shortcut(kp.mods)) return true;
    switch (kp.key) {
    case k::a: case k::c: case k::v: case k::x: case k::y: case k::z:
    case k::left: case k::right: case k::up: case k::down: case k::home: case k::end:
    case k::backspace: case k::delete_:
        return true;
    default:
        return false;
    }
}

} // namespace prism::ui
