#pragma once

// What a console says about a run: how long it has been going, how far it got, and the three
// captions its widgets show. A run's own thread never touches any of this -- it only sends lines.

#include "clock.hpp"
#include "console_log.hpp"
#include "text.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include <fmt/format.h>

namespace prism::runner {

struct RunStatus {
    unsigned launches = 0;
    steady::time_point started{};
    double elapsed_s = 0.0;
    std::optional<int> exit_code{};
    bool stopped = false;
    bool running = false;

    // A new run: another launch, counting from now, and nothing known about how it ends.
    void begin(steady::time_point now) {
        ++launches;
        started = now;
        elapsed_s = 0.0;
        exit_code = std::nullopt;
        stopped = false;
        running = true;
    }

    // The capture's frame: a run that has already been going for `back_s`.
    void seed(steady::time_point now, double back_s) { begin(now - seconds(back_s)); }

    double elapsed(steady::time_point now) const {
        if (!running) return elapsed_s;
        return std::chrono::duration<double>(now - started).count();
    }

    // The run is over: freeze the elapsed time before the flag that makes it live says otherwise.
    void finish(steady::time_point now, int code, bool was_stopped) {
        elapsed_s = elapsed(now);
        running = false;
        exit_code = code;
        stopped = was_stopped;
    }

    // A run the app declined to start: no launch counted, and the exit code says it did not run.
    void refuse() {
        elapsed_s = 0.0;
        running = false;
        exit_code = 127;
        stopped = false;
    }
};

// The bar's value when a run ends: what its output reported, or what its exit code implies.
inline double final_progress(const ConsoleLog& log, const RunStatus& status) {
    if (log.saw_progress()) return log.progress();
    return status.exit_code.value_or(1) == 0 ? 1.0 : 0.0;
}

inline std::string outcome_label(const RunStatus& status) {
    if (status.stopped) return fmt::format("stopped after {:.1f} s", status.elapsed_s);
    if (!status.exit_code || *status.exit_code != 0)
        return fmt::format("failed (exit {}) after {:.1f} s", status.exit_code.value_or(-1), status.elapsed_s);
    return fmt::format("done in {:.1f} s", status.elapsed_s);
}

struct ProgressState {
    double value = 0.0;
    std::string label{};
    bool busy = false;
};

// The bar while a run is going: its own progress when the output reported one, the line count
// when it has not got that far yet.
inline ProgressState progress_of(const ConsoleLog& log, const RunStatus& status, steady::time_point now) {
    const double seconds = status.elapsed(now);
    if (!log.saw_progress())
        return {.value = 0.0,
                .label = fmt::format("{} lines    {:.1f} s", grouped(log.size()), seconds),
                .busy = true};
    return {.value = log.progress(),
            .label = fmt::format("{:.0f}%    {} lines    {:.1f} s", log.progress() * 100.0, grouped(log.size()),
                                 seconds)};
}

// The line under the bar: what the console holds, what it costs, and how much of it is shown.
inline std::string stats_of(const ConsoleLog& log, const RunStatus& status, std::size_t shown,
                            steady::time_point now) {
    const std::size_t total = log.size();
    if (total == 0) return "no output yet";
    const double seconds = status.elapsed(now);
    const double rate = seconds > 0.0 ? static_cast<double>(total) / seconds : 0.0;
    const std::string problems = (log.warnings() || log.errors())
        ? fmt::format("    {} warnings, {} error{}", log.warnings(), log.errors(), log.errors() == 1 ? "" : "s")
        : std::string{};
    const std::string filtered = shown == total ? std::string{} : fmt::format("    {} shown", grouped(shown));
    return fmt::format("{} lines{}{}    {:.0f} lines/s    {:.1f} s", grouped(total), problems, filtered, rate,
                       seconds);
}

inline std::string command_header(const std::string& command, unsigned run) {
    return fmt::format("$ {}    (run {})", command, run);
}

// The console's last word on a run.
inline std::string exit_line(const RunStatus& status, std::size_t lines) {
    return fmt::format("exit {}    {} lines in {:.1f} s", status.exit_code.value_or(-1), grouped(lines),
                       status.elapsed_s);
}

} // namespace prism::runner
