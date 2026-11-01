#pragma once

// The notes editor's logic without any UI: file I/O, statistics and search. Tested in
// tests/test_notes_logic.cpp; the app (model_notes.cpp) only wires these to widgets.

#include <prism/core/utf8.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace notes {

inline std::expected<std::string, std::string> load_text(const std::filesystem::path& path,
                                                         std::size_t max_bytes = 1 << 20) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return std::unexpected("cannot read: " + ec.message());
    if (size > max_bytes) return std::unexpected("too large to edit here");
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::unexpected("cannot open");
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (text.find('\0') != std::string::npos) return std::unexpected("binary file");
    return text;
}

inline std::expected<void, std::string> save_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected("cannot open for writing");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) return std::unexpected("write failed");
    return {};
}

struct Stats {
    std::size_t lines = 0;
    std::size_t characters = 0;
    bool operator==(const Stats&) const = default;
};

inline Stats stats(std::string_view text) {
    if (text.empty()) return {};
    auto newlines = static_cast<std::size_t>(std::ranges::count(text, '\n'));
    bool open_last_line = text.back() != '\n';
    return {newlines + (open_last_line ? 1 : 0), prism::core::utf8::count(text)};
}

// Byte offsets of every non-overlapping occurrence of `query`.
inline std::vector<std::size_t> find_all(std::string_view text, std::string_view query) {
    std::vector<std::size_t> hits;
    if (query.empty()) return hits;
    for (auto pos = text.find(query); pos != std::string_view::npos; pos = text.find(query, pos + query.size()))
        hits.push_back(pos);
    return hits;
}

inline std::string status_line(const std::string& name, Stats s, bool modified,
                               std::optional<std::size_t> matches) {
    if (name.empty()) return "No file open";
    std::string out = name + (modified ? " (modified)" : "") + "  |  " + std::to_string(s.lines) +
                      " lines, " + std::to_string(s.characters) + " characters";
    if (matches) out += "  |  " + std::to_string(*matches) + " matches";
    return out;
}

} // namespace notes
