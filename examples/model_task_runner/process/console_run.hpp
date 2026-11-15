#pragma once

// One console's state, and the thread that fills it: what the run has said, how it went, and the
// thread that produced it. Plain C++ -- the batch sink is the only way out of the thread, so it
// never reaches a Field.
//
// Every method that adds lines gives back the ones the console's filter keeps, which is what the
// block appends to its list.

#include "clock.hpp"
#include "console_log.hpp"
#include "run_job.hpp"
#include "run_status.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace prism::runner {

namespace run_detail {

// The kept lines of a single line the console says itself.
inline std::vector<ConsoleLine> one(std::optional<ConsoleLine> line) {
    std::vector<ConsoleLine> kept;
    if (line) kept.push_back(std::move(*line));
    return kept;
}

inline void absorb(std::vector<ConsoleLine>& into, std::vector<ConsoleLine> lines) {
    for (auto& line : lines) into.push_back(std::move(line));
}

} // namespace run_detail

class ConsoleRun {
public:
    // A new launch, counting from now, with the command header as its first line.
    std::vector<ConsoleLine> begin(const std::string& command, const LineFilter& filter) {
        log_.clear();
        status_.begin(steady::now());
        return run_detail::one(log_.say(command_header(command, status_.launches), LineKind::Command, filter));
    }

    // The capture's run: a launch that has already been going for `back_s`, its header plain --
    // a screenshot shows what the console says, not which run it is.
    std::vector<ConsoleLine> seed(const std::string& command, double back_s, const LineFilter& filter) {
        log_.clear();
        status_.seed(steady::now(), back_s);
        return run_detail::one(log_.say("$ " + command, LineKind::Command, filter));
    }

    // Starts the thread for the launch `begin` just counted.
    void start(std::size_t task, const std::optional<SimKind>& script, const std::string& command, BatchSink sink) {
        const JobSpec job{.task = task,
                          .run = status_.launches,
                          .script = script,
                          .command = command,
                          .start = status_.started};
        worker_ = std::jthread(
            [job, sink = std::move(sink)](std::stop_token token) { run_job(job, token, sink); });
    }

    // Joining takes as long as the process takes to die, which is why a stop is not "set a flag
    // and hope": the run is being killed, so this is a fraction of a second, not the run's length.
    void stop() {
        if (!worker_.joinable()) return;
        worker_.request_stop();
        worker_.join();
    }

    // A run the app declines to start: the console says why, in the console's own language.
    std::vector<ConsoleLine> refuse(const std::string& command, std::string reason, const LineFilter& filter) {
        log_.clear();
        status_.refuse();
        std::vector<ConsoleLine> kept = run_detail::one(log_.say("$ " + command, LineKind::Command, filter));
        run_detail::absorb(kept, run_detail::one(log_.say(std::move(reason), LineKind::Error, filter)));
        return kept;
    }

    // One delivery from the run's thread; nothing back when it belongs to a run that was replaced.
    std::optional<std::vector<ConsoleLine>> ingest(const OutputBatch& batch, const LineFilter& filter) {
        if (batch.run != status_.launches) return std::nullopt;
        std::vector<ConsoleLine> kept = log_.append(batch.lines, filter);
        if (batch.finished) run_detail::absorb(kept, finish(batch.exit_code, batch.stopped, filter));
        return kept;
    }

    // The console's last word on a run.
    std::vector<ConsoleLine> finish(int code, bool stopped, const LineFilter& filter) {
        status_.finish(steady::now(), code, stopped);
        return run_detail::one(log_.say(exit_line(status_, log_.size()),
                                        code == 0 ? LineKind::Success : LineKind::Error, filter));
    }

    const ConsoleLog& log() const { return log_; }
    ConsoleLog& log() { return log_; }
    const RunStatus& status() const { return status_; }

private:
    ConsoleLog log_{};     // the run, as it came
    RunStatus status_{};   // how long it has been going and how it ended
    std::jthread worker_{};
};

} // namespace prism::runner
