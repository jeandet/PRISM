#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Byte-offset <-> code point helpers for text editing. Offsets are byte indices into UTF-8;
// every function clamps out-of-range input and never reads past the end, even on malformed text.
//
// Steps are code points. The caret and deletion step by user-perceived character instead
// (ui/text_segmentation.hpp, UAX #29): use that for anything a user moves through.
namespace prism::core::utf8 {

inline bool is_continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Start of the code point containing byte i (i clamped to the string size).
inline std::size_t snap(std::string_view s, std::size_t i) {
    i = std::min(i, s.size());
    while (i > 0 && i < s.size() && is_continuation(s[i])) --i;
    return i;
}

// Start of the code point after the one at i.
inline std::size_t next(std::string_view s, std::size_t i) {
    i = snap(s, i);
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && is_continuation(s[i])) ++i;
    return i;
}

// Start of the code point before the one at i.
inline std::size_t prev(std::string_view s, std::size_t i) {
    i = snap(s, i);
    if (i == 0) return 0;
    return snap(s, i - 1);
}

// Number of code points in s[0, i).
inline std::size_t count_upto(std::string_view s, std::size_t i) {
    s = s.substr(0, std::min(i, s.size()));
    return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return !is_continuation(c); }));
}

inline std::size_t count(std::string_view s) { return count_upto(s, s.size()); }

// Byte offset of the code point at index cp (clamped to the string size).
inline std::size_t offset(std::string_view s, std::size_t cp) {
    std::size_t i = 0;
    for (; cp > 0 && i < s.size(); --cp) i = next(s, i);
    return i;
}

// Valid UTF-8 without NUL bytes: NULs are dropped, invalid or overlong sequences become U+FFFD.
// For text from outside (the clipboard); SDL's own text input is already valid.
inline std::string sanitize(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = byte(i);
        if (c == 0) { ++i; continue; }
        std::size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        bool ok = len > 0 && i + len <= s.size();
        for (std::size_t k = 1; ok && k < len; ++k) ok = (byte(i + k) & 0xC0) == 0x80;
        if (ok && len > 1) {   // reject overlong forms, surrogates and code points above U+10FFFF
            std::uint32_t cp = c & (0xFF >> (len + 1));
            for (std::size_t k = 1; k < len; ++k) cp = (cp << 6) | (byte(i + k) & 0x3F);
            static constexpr std::uint32_t min_cp[] = {0, 0, 0x80, 0x800, 0x10000};
            ok = cp >= min_cp[len] && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
        }
        if (ok) { out.append(s.substr(i, len)); i += len; }
        else { out += "\xEF\xBF\xBD"; ++i; }
    }
    return out;
}

} // namespace prism::core::utf8
