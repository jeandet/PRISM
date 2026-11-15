#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

// Turning SQLite's complaint into a place in the text the user typed.
namespace prism::data_studio {

struct ByteRange {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool operator==(const ByteRange&) const = default;
};

// SQLite names what it choked on -- near "FROMM": syntax error, no such column: kmm -- so the
// editor can underline that token instead of only printing the message.
inline std::optional<ByteRange> error_mark(std::string_view sql, std::string_view message) {
    auto locate = [&](std::string_view token) -> std::optional<ByteRange> {
        if (token.empty()) return std::nullopt;
        auto at = sql.find(token);
        if (at == std::string_view::npos) return std::nullopt;
        return ByteRange{at, at + token.size()};
    };

    if (auto at = message.find("near \""); at != std::string_view::npos) {
        auto begin = at + 6;
        auto end = message.find('"', begin);
        if (end != std::string_view::npos) return locate(message.substr(begin, end - begin));
    }
    for (auto marker : {"no such column: ", "no such table: "}) {
        if (auto at = message.find(marker); at != std::string_view::npos) {
            auto rest = message.substr(at + std::string_view{marker}.size());
            return locate(rest.substr(0, rest.find_first_of(" ,")));
        }
    }
    return std::nullopt;
}

} // namespace prism::data_studio
