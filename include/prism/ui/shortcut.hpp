#pragma once

#include <prism/input/input_event.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

struct NamedKey {
    std::int32_t key;
    std::string_view name;
};

// Keys known by name, as shown beside menu items and read back by parse_shortcut.
inline constexpr NamedKey named_keys[] = {
    {input::keys::delete_, "Delete"}, {input::keys::backspace, "Backspace"}, {input::keys::enter, "Enter"},
    {input::keys::escape, "Esc"},     {input::keys::tab, "Tab"},             {input::keys::space, "Space"},
    {input::keys::home, "Home"},      {input::keys::end, "End"},             {input::keys::page_up, "PgUp"},
    {input::keys::page_down, "PgDown"}, {input::keys::left, "Left"},        {input::keys::right, "Right"},
    {input::keys::up, "Up"},          {input::keys::down, "Down"},
};

inline bool same_letters(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

inline std::string key_name(std::int32_t key) {
    if (key >= 'a' && key <= 'z') return std::string(1, static_cast<char>(key - 'a' + 'A'));
    if (key >= '0' && key <= '9') return std::string(1, static_cast<char>(key));
    if (key >= input::keys::f1 && key <= input::keys::f12) return "F" + std::to_string(key - input::keys::f1 + 1);
    for (auto& k : named_keys)
        if (k.key == key) return std::string(k.name);
    return key > 32 && key < 127 ? std::string(1, static_cast<char>(key)) : "?";
}

inline std::optional<std::int32_t> key_from_name(std::string_view name) {
    if (name.size() == 1) {
        auto c = static_cast<unsigned char>(name[0]);
        if (std::isalpha(c)) return static_cast<std::int32_t>(std::tolower(c));
        if (std::isdigit(c)) return static_cast<std::int32_t>(c);
    }
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        int n = 0;
        auto [end, ec] = std::from_chars(name.data() + 1, name.data() + name.size(), n);
        if (ec == std::errc{} && end == name.data() + name.size() && n >= 1 && n <= 12) return input::keys::f1 + n - 1;
    }
    for (auto& k : named_keys)
        if (same_letters(k.name, name)) return k.key;
    return std::nullopt;
}

inline std::optional<std::uint16_t> modifier_from_name(std::string_view name) {
    if (same_letters(name, "Ctrl") || same_letters(name, "Cmd")) return input::mods::ctrl;   // Cmd: the Mac's Ctrl
    if (same_letters(name, "Shift")) return input::mods::shift;
    if (same_letters(name, "Alt")) return input::mods::alt;
    return std::nullopt;
}
} // namespace shortcut_detail

inline bool matches(Shortcut s, const input::KeyPress& kp) {
    return s.key != 0 && kp.key == s.key
        && shortcut_detail::normalized(kp.mods) == shortcut_detail::normalized(s.mods);
}

// Reads "Ctrl+Shift+S", "Cmd+O" or "F5" (any letter case): modifiers, then one key. Nothing for
// a name it does not know.
inline std::optional<Shortcut> parse_shortcut(std::string_view text) {
    Shortcut out;
    while (true) {
        auto plus = text.find('+');
        auto part = text.substr(0, plus);
        if (plus == std::string_view::npos) {
            auto key = shortcut_detail::key_from_name(part);
            if (!key) return std::nullopt;
            out.key = *key;
            return out;
        }
        auto mod = shortcut_detail::modifier_from_name(part);
        if (!mod) return std::nullopt;
        out.mods |= *mod;
        text.remove_prefix(plus + 1);
    }
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
