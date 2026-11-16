#pragma once

// One task's console: the filter over its lines, the stop button, the progress bar, and the run
// all of that is about.
//
// The run itself -- its log, its stopwatch, its thread -- is `process/console_run.hpp`, which is
// plain C++: all this block does is place widgets and paint what the run says, on the logic
// thread, where every Field must be written.

#include <prism/prism.hpp>

#include "../process/catalog.hpp"
#include "../process/console_run.hpp"
#include "../process/run_job.hpp"
#include "../process/run_status.hpp"
#include "console_rows.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prism::runner::blocks {

struct ConsolePane {
    core::Field<ui::ProgressBar> progress{{.label = "not run yet"}};
    core::Field<ui::Label<>> stats{{"no output yet"}};
    core::Field<ui::TextField<>> filter{{.placeholder = "Filter lines"}};
    core::Field<ui::Checkbox> errors_only{{.label = "Errors and warnings"}};
    core::Field<ui::Button> stop{{.text = "Stop", .primary = false}};
    core::Field<bool> running{false};

    core::List<ConsoleLine> rows{};   // what the console shows: the lines the filter keeps
    ConsoleRun run{};

    ConsolePane() {
        reactions_.push_back(filter.on_change().connect([this](const auto&) { refilter(); }));
        reactions_.push_back(errors_only.on_change().connect([this](const auto&) { refilter(); }));
        reactions_.push_back(stop.on_change().connect([this](const auto&) { run.stop(); }));
    }

    // One console per task, showing its latest run: starting a task again stops the previous run
    // and starts over, which is also what keeps 200 000 line runs bounded in memory.
    void launch(std::size_t task, const std::optional<SimKind>& script, const std::string& command, BatchSink sink) {
        run.stop();
        rows.replace_all(std::vector<ConsoleLine>{});
        keep(run.begin(command, filter_now()));
        progress.set({.value = 0.0, .label = "starting", .busy = true});
        sync_stats();
        mirror_running();
        run.start(task, script, command, std::move(sink));
    }

    void refuse(const std::string& command, std::string reason) {
        rows.replace_all(std::vector<ConsoleLine>{});
        keep(run.refuse(command, std::move(reason), filter_now()));
        progress.set({.value = 0.0, .label = "not started"});
        sync_stats();
        mirror_running();
    }

    // One delivery from a run's thread; false when it belongs to a run that was replaced.
    bool ingest(const OutputBatch& batch) {
        auto kept = run.ingest(batch, filter_now());
        if (!kept) return false;
        keep(std::move(*kept));
        sync_bar();
        sync_stats();
        return true;
    }

    // Per frame, while it runs: how long it has been going and how much it has said are the
    // frame's business, not the output's.
    void tick() {
        if (!run.status().running) return;
        sync_bar();
        sync_stats();
    }

    // --- what the rest of the app reads ---------------------------------------------------

    bool is_running() const { return run.status().running; }
    unsigned launches() const { return run.status().launches; }
    std::size_t lines() const { return run.log().size(); }

    console_rows::TaskState state() const {
        const auto& status = run.status();
        if (status.running) return console_rows::TaskState::Running;
        if (!status.exit_code) return console_rows::TaskState::Idle;
        if (status.stopped) return console_rows::TaskState::Stopped;
        return *status.exit_code == 0 ? console_rows::TaskState::Passed : console_rows::TaskState::Failed;
    }

    // --- the frames the capture shows -----------------------------------------------------

    // A run that has just finished: every colour a console has, an exit code, and a progress bar
    // that knows how far it got. Nothing here waits on a clock -- a screenshot has no clock to
    // tick, and a thread would be a thread running during one.
    void seed_finished(const SimRun& script, const std::string& command) {
        seed(script, command, script.line_count, script.duration_s);
        keep(run.finish(script.exit_code, false, filter_now()));
        sync_bar();
        sync_stats();
    }

    // A run still going, `back_s` into its own elapsed time.
    void seed_running(const SimRun& script, const std::string& command, std::size_t count, double back_s) {
        seed(script, command, count, back_s);
        sync_bar();
        sync_stats();
    }

    void view(app::ViewBuilder& p) {
        p.vstack([&] {
            p.hstack([&] {
                p.widget(filter).tooltip("Show only the lines that contain this text");
                p.widget(errors_only).tooltip("Hide everything that is not an error or a warning");
                p.spacer();
                p.widget(stop).tooltip("Stop the run: SIGTERM, then SIGKILL").enabled(running);
            });
            p.widget(progress);
            p.widget(stats);
            p.list(rows).follow_tail();   // Widget<ConsoleLine>: a row's look is a specialization
        });
    }

private:
    void seed(const SimRun& script, const std::string& command, std::size_t count, double back_s) {
        rows.replace_all(std::vector<ConsoleLine>{});
        keep(run.seed(command, back_s, filter_now()));
        keep(run.log().append(simulated_lines(script, count), filter_now()));
        mirror_running();
    }

    // The bar while a run goes: its own progress when the output reported one, the line count
    // when it has not got that far yet. Once it is over, what the exit code implies.
    void sync_bar() {
        const auto& status = run.status();
        if (!status.running) {
            progress.set({.value = final_progress(run.log(), status), .label = outcome_label(status)});
            return;
        }
        const auto bar = progress_of(run.log(), status, steady::now());
        progress.set({.value = bar.value, .label = bar.label, .busy = bar.busy});
    }

    void sync_stats() { stats.set({stats_of(run.log(), run.status(), rows.size(), steady::now())}); }

    void mirror_running() { running.set(run.status().running); }

    void keep(std::vector<ConsoleLine> lines) {
        for (auto& line : lines) rows.push_back(std::move(line));
    }

    // Filtering is a scan of what the console holds: a few milliseconds at 200 000 lines, on a
    // keystroke. Incremental filtering would be the next step, and is not needed at this size.
    void refilter() {
        rows.replace_all(run.log().visible(filter_now()));
        sync_stats();
    }

    LineFilter filter_now() const {
        return {.needle = value(filter), .errors_only = value(errors_only)};
    }

    std::vector<core::Connection> reactions_{};
};

} // namespace prism::runner::blocks
