#pragma once

// What a run said, and the two things a console reads out of it: the counts its filter line
// shows, and the last progress the output reported.
//
// `say` is for the console's own lines -- the command header, an exit code, why a run could not
// start -- which move neither the counts nor the progress.

#include "lines.hpp"
#include "progress.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism::runner {

class ConsoleLog {
public:
    // What `filter` keeps of `texts`, as they are appended. Filtering incrementally would be the
    // next step, and is not needed at this size: the whole log refilters in a few milliseconds.
    std::vector<ConsoleLine> append(const std::vector<std::string>& texts, const LineFilter& filter) {
        std::vector<ConsoleLine> kept;
        kept.reserve(texts.size());
        for (const auto& text : texts) {
            const ConsoleLine line{text, classify_line(text)};
            lines_.push_back(line);
            if (filter.keeps(line.text, line.kind)) kept.push_back(line);
            if (line.kind == LineKind::Error) ++errors_;
            if (line.kind == LineKind::Warning) ++warnings_;
            if (auto done = parse_progress(line.text)) {
                progress_ = *done;
                saw_progress_ = true;
            }
        }
        return kept;
    }

    // One line the console says itself; what it returns is the line to show, if any.
    std::optional<ConsoleLine> say(std::string text, LineKind kind, const LineFilter& filter) {
        const ConsoleLine line{std::move(text), kind};
        lines_.push_back(line);
        if (!filter.keeps(line.text, line.kind)) return std::nullopt;
        return line;
    }

    std::vector<ConsoleLine> visible(const LineFilter& filter) const { return filtered_lines(lines_, filter); }

    void clear() {
        lines_.clear();
        warnings_ = 0;
        errors_ = 0;
        saw_progress_ = false;
        progress_ = 0.0;
    }

    std::size_t size() const { return lines_.size(); }
    std::size_t warnings() const { return warnings_; }
    std::size_t errors() const { return errors_; }
    bool saw_progress() const { return saw_progress_; }
    double progress() const { return progress_; }

private:
    std::vector<ConsoleLine> lines_{};
    std::size_t warnings_ = 0;
    std::size_t errors_ = 0;
    bool saw_progress_ = false;
    double progress_ = 0.0;
};

} // namespace prism::runner
