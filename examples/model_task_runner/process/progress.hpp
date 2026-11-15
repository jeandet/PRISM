#pragma once

// Reading how far a run got out of its own output: ninja, ctest and meson all tag their lines
// with something like `[ 42%]` or `[3/120]`, and that is the only progress a build console has.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

namespace prism::runner {

inline std::size_t skip_spaces(std::string_view text, std::size_t i) {
    while (i < text.size() && text[i] == ' ') ++i;
    return i;
}

// The number starting at `i`, and where it ends; -1 when there is none.
inline std::pair<double, std::size_t> read_number(std::string_view text, std::size_t i) {
    double value = 0.0;
    std::size_t digits = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i, ++digits)
        value = value * 10.0 + static_cast<double>(text[i] - '0');
    return {digits > 0 ? value : -1.0, i};
}

// ` 42%` or `  3/120` inside a bracket, as the fraction done.
inline std::optional<double> parse_progress_body(std::string_view inner) {
    auto i = skip_spaces(inner, 0);
    auto [first, after] = read_number(inner, i);
    if (first < 0.0) return std::nullopt;
    i = skip_spaces(inner, after);
    if (i < inner.size() && inner[i] == '%') {
        if (skip_spaces(inner, i + 1) != inner.size()) return std::nullopt;
        return std::clamp(first / 100.0, 0.0, 1.0);
    }
    if (i < inner.size() && inner[i] == '/') {
        auto [total, after_total] = read_number(inner, skip_spaces(inner, i + 1));
        if (total <= 0.0 || skip_spaces(inner, after_total) != inner.size()) return std::nullopt;
        return std::clamp(first / total, 0.0, 1.0);
    }
    return std::nullopt;
}

// The first `[...]` in the line that reads as progress: ninja, ctest and meson all tag their
// lines this way.
inline std::optional<double> parse_progress(std::string_view line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '[') continue;
        const auto close = line.find(']', i + 1);
        if (close == std::string_view::npos) return std::nullopt;
        if (auto done = parse_progress_body(line.substr(i + 1, close - i - 1))) return done;
        i = close;
    }
    return std::nullopt;
}

} // namespace prism::runner
