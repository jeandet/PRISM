#pragma once

#include <cstdint>
#include <format>
#include <string>

#include "result_set.hpp"

// What the status bar says, and how it says it. Pure wording, so the line a user reads is a
// function of two numbers and can be tested without a window.
namespace prism::data_studio {

enum class StatusKind : std::uint8_t { Idle, Loading, Running, Cancelling, Done, Error, Cancelled };

struct Status {
    StatusKind kind = StatusKind::Loading;
    std::string text = "building the trips table...";
    double progress = 0.0;   // Loading: 0..1 done
    bool operator==(const Status&) const = default;
};

inline bool working(StatusKind kind) {
    return kind == StatusKind::Loading || kind == StatusKind::Running || kind == StatusKind::Cancelling;
}

// Errors have to read as errors in a line of plain text: word them, don't just colour them.
inline std::string status_line(const Status& status) {
    return status.kind == StatusKind::Error ? "Error: " + status.text : status.text;
}

inline std::string elapsed_text(double seconds) {
    if (seconds < 1.0) return std::format("{} ms", std::llround(seconds * 1000.0));
    return std::format("{:.1f} s", seconds);
}

inline std::string rows_text(std::size_t rows) {
    return std::format("{} {}", with_thousands(static_cast<std::int64_t>(rows)), rows == 1 ? "row" : "rows");
}

inline std::string done_status(std::size_t rows, double seconds) {
    return std::format("{} in {}", rows_text(rows), elapsed_text(seconds));
}

inline std::string running_status(double seconds) { return std::format("running {}...", elapsed_text(seconds)); }

inline std::string cancelled_status(double seconds) { return std::format("cancelled after {}", elapsed_text(seconds)); }

} // namespace prism::data_studio
