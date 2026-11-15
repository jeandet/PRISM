#pragma once

// A run's own thread: the generated script, or a real process, turned into the batches the model
// ingests. Nothing here touches a Field -- the only way out is the sink, and the only way in is
// the stop token.

#include "child_process.hpp"
#include "clock.hpp"
#include "lines.hpp"
#include "platform.hpp"
#include "simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace prism::runner {

// What a run's thread sends the model: the lines it has so far, and at the end, how it went.
// `run` names the launch, so a stopped run's last delivery cannot land in the console that
// replaced it.
struct OutputBatch {
    std::size_t task = 0;
    unsigned run = 0;
    std::vector<std::string> lines{};
    bool finished = false;
    int exit_code = 0;
    bool stopped = false;
};

// A run hands its lines over in batches: one crossing per publish instead of one per line is
// what lets the console keep up with 200 000 lines. The interval bounds how late a slow run's
// output can be; the count bounds how big a flood's batches get.
inline constexpr std::size_t kBatchLines = 4096;
inline constexpr std::chrono::milliseconds kBatchInterval{16};
// How long a waiting run sleeps at a time: short enough that a stop request is noticed.
inline constexpr std::chrono::milliseconds kStopSlice{4};
inline constexpr int kStoppedCode = 143;   // what a shell reports for a process killed by SIGTERM

// Sleeps until `due`, in slices short enough that a stop request is noticed. False: stop asked.
inline bool wait_until(steady::time_point due, const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        const auto left = due - steady::now();
        if (left <= steady::duration::zero()) return true;
        std::this_thread::sleep_for(std::min(left, steady::duration{kStopSlice}));
    }
    return false;
}

// What a run's thread is asked to do. A `script` runs generated lines; without one, `command`
// runs for real -- and the caller never asks for that where there is no shell.
struct JobSpec {
    std::size_t task = 0;
    unsigned run = 0;
    std::optional<SimKind> script{};
    std::string command{};
    steady::time_point start{};
};

using BatchSink = std::function<void(OutputBatch)>;

namespace job_detail {

// The batching both kinds of run share, so a script and a process reach the model the same way.
class Batcher {
public:
    Batcher(const JobSpec& job, const BatchSink& send) : job_(job), send_(send) {}

    void add(std::string line) { held_.push_back(std::move(line)); }

    // A batch is worth a crossing when it is full, or when the last one is old enough.
    void flush_if_due() {
        if (held_.empty()) return;
        if (held_.size() < kBatchLines && steady::now() - last_send_ < kBatchInterval) return;
        send_(batch(false, 0, false));
        last_send_ = steady::now();
    }

    void finish(int exit_code) { send_(batch(true, exit_code, false)); }

    void stopped(int exit_code = kStoppedCode) { send_(batch(true, exit_code, true)); }

private:
    OutputBatch batch(bool finished, int exit_code, bool was_stopped) {
        return OutputBatch{.task = job_.task,
                           .run = job_.run,
                           .lines = std::exchange(held_, {}),
                           .finished = finished,
                           .exit_code = exit_code,
                           .stopped = was_stopped};
    }

    const JobSpec& job_;
    const BatchSink& send_;
    std::vector<std::string> held_{};
    steady::time_point last_send_{steady::now()};
};

// A generated run: the script says when each line is due, so a build takes ten seconds and a
// flood takes what the console takes.
inline void read_script(const JobSpec& job, job_detail::Batcher& batcher, const std::stop_token& stop) {
    const SimRun script = simulated_run(*job.script, job.run);
    for (std::size_t i = 0; i < script.line_count; ++i) {
        const double due_s = simulated_time(script, i);
        if (due_s > 0.0 && !wait_until(job.start + seconds(due_s), stop)) return batcher.stopped();
        batcher.add(simulated_line(script, i));
        batcher.flush_if_due();
    }
    batcher.finish(script.exit_code);
}

#if PRISM_TASK_RUNNER_POSIX
// A real run: bytes off the pipe, cut into lines, batched, and killed if the user says so.
inline void read_process(const JobSpec& job, job_detail::Batcher& batcher, const std::stop_token& stop) {
    std::string error;
    auto child = ChildProcess::spawn_shell(job.command, error);
    if (!child) {
        batcher.add(std::move(error));
        return batcher.finish(127);
    }

    LineAssembler assembler;
    std::vector<std::string> decoded;
    char buffer[4096];
    while (!stop.stop_requested()) {
        const auto got = child->read_some(buffer, sizeof buffer);
        if (got == 0) break;   // the output is over: the child is done
        if (got > 0) {
            decoded.clear();
            decode_chunk(assembler, std::string_view(buffer, static_cast<std::size_t>(got)), decoded);
            for (auto& line : decoded) batcher.add(std::move(line));
        }
        batcher.flush_if_due();
    }

    if (stop.stop_requested()) return batcher.stopped(child->stop(std::chrono::milliseconds{200}));
    for (auto& line : assembler.finish()) batcher.add(strip_ansi(line));
    batcher.finish(child->wait());
}
#endif

} // namespace job_detail

// What a run's thread does, from the thread's point of view: one call, and the model has the
// first batch before the frame that started it has been drawn.
inline void run_job(const JobSpec& job, const std::stop_token& stop, const BatchSink& send) {
    job_detail::Batcher batcher{job, send};
    if (job.script) return job_detail::read_script(job, batcher, stop);
#if PRISM_TASK_RUNNER_POSIX
    job_detail::read_process(job, batcher, stop);
#endif
}

} // namespace prism::runner
