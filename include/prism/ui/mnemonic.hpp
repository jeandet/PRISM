#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Keyboard mnemonics in menu labels, as in Qt and Windows: "&File" shows "File" with the F
// underlined, and Alt+F opens it; "&&" is a literal "&".
namespace prism::ui::menu_detail {

struct Mnemonic {
    std::string text;                           // the label as shown
    std::size_t underline = std::string::npos;  // byte offset of the marked character in text
};

inline Mnemonic parse_mnemonic(std::string_view label) {
    Mnemonic m;
    m.text.reserve(label.size());
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '&' && i + 1 < label.size()) {
            ++i;   // the marker goes; "&&" leaves one "&", which marks nothing
            if (label[i] != '&' && m.underline == std::string::npos) m.underline = m.text.size();
        }
        m.text += label[i];
    }
    return m;
}

inline bool is_mnemonic_key(std::int32_t key) {
    return (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9');
}

// The key that triggers the label's mnemonic (SDL key codes are lowercase ASCII), or 0.
// simplify: ASCII letters and digits only; a non-ASCII mnemonic needs the key's character
// from the backend (SDL_GetKeyFromScancode with the layout), not the raw key code.
inline std::int32_t mnemonic_key(std::string_view label) {
    auto m = parse_mnemonic(label);
    if (m.underline == std::string::npos) return 0;
    char c = m.text[m.underline];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return is_mnemonic_key(c) ? c : 0;
}

} // namespace prism::ui::menu_detail

namespace prism::ui {
// A menu label as shown, without its "&" marker: compare a chosen item against this.
inline std::string menu_text(std::string_view label) { return menu_detail::parse_mnemonic(label).text; }
}
