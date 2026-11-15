#pragma once

// What a console line is, and what of it a filter lets through: a line is a value with a kind,
// and telling an error from a warning is a function of its text.

#include "text.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prism::runner {

enum class LineKind : std::uint8_t { Info, Command, Success, Warning, Error };

// First match wins, so an error that happens to mention a warning stays an error.
inline LineKind classify_line(std::string_view line) {
    if (contains_icase(line, "error") || contains_icase(line, "failed") || contains_icase(line, "failure"))
        return LineKind::Error;
    if (contains_icase(line, "warning") || contains_icase(line, "warn:")) return LineKind::Warning;
    if (contains_icase(line, "passed") || contains_icase(line, "ok ") || contains_icase(line, "finished")
        || contains_icase(line, "succeeded") || contains_icase(line, "done"))
        return LineKind::Success;
    if (!line.empty() && line.front() == '$') return LineKind::Command;
    return LineKind::Info;
}

struct ConsoleLine {
    std::string text{};
    LineKind kind = LineKind::Info;
    bool operator==(const ConsoleLine&) const = default;
};

// What the console shows of what a run said.
struct LineFilter {
    std::string needle{};        // empty: everything
    bool errors_only = false;    // errors and warnings only

    bool keeps(std::string_view text, LineKind kind) const {
        if (errors_only && kind != LineKind::Error && kind != LineKind::Warning) return false;
        return contains_icase(text, needle);
    }
    bool operator==(const LineFilter&) const = default;
};

// What the console shows of what a run said: the lines the filter keeps, in order. Runs on every
// filter edit, hence the reserve -- a 200 000-line console refilters in a few milliseconds.
inline std::vector<ConsoleLine> filtered_lines(const std::vector<ConsoleLine>& lines, const LineFilter& filter) {
    std::vector<ConsoleLine> kept;
    kept.reserve(lines.size());
    for (const auto& line : lines)
        if (filter.keeps(line.text, line.kind)) kept.push_back(line);
    return kept;
}

} // namespace prism::runner
