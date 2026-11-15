#include <prism/prism.hpp>
#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>
#include <prism/ui/dock.hpp>
#include "console_rows.hpp"
#include "process.hpp"
#include "runner_model.hpp"
#include "../showcase/showcase_common.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace prism;

namespace {

using steady = std::chrono::steady_clock;

// A run hands its lines over in batches: one crossing per publish instead of one per line is
// what lets the console keep up with 200 000 lines. The interval bounds how late a slow run's
// output can be; the count bounds how big a flood's batches get.
constexpr std::size_t kBatchLines = 4096;
constexpr auto kBatchInterval = std::chrono::milliseconds{16};
constexpr auto kStopSlice = std::chrono::milliseconds{4};
constexpr int kStoppedCode = 143;   // what a shell reports for a process killed by SIGTERM

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

// 200000 reads as 200 000: these numbers are the point of the console.
std::string grouped(std::size_t value) {
    std::string text = std::to_string(value);
    for (auto at = static_cast<std::ptrdiff_t>(text.size()) - 3; at > 0; at -= 3)
        text.insert(static_cast<std::size_t>(at), " ");
    return text;
}

// Sleeps until `due`, in slices short enough that a stop request is noticed. False: stop asked.
bool wait_until(steady::time_point due, const std::stop_token& stop) {
    while (!stop.stop_requested()) {
        const auto left = due - steady::now();
        if (left <= steady::duration::zero()) return true;
        std::this_thread::sleep_for(std::min(left, steady::duration{kStopSlice}));
    }
    return false;
}

} // namespace

// Glaze reflects this struct: every member is a key, a nested struct a table. Keep it out of an
// anonymous namespace (Glaze needs a type with linkage).
struct TaskRunnerPrefs {
    core::Field<ui::DockLayout> layout{{ui::dock_split(/*vertical=*/false, {
        ui::dock_group({"tasks"}),
        ui::dock_group({"console"})
    }, {0.30f, 0.70f})}};
    app::WindowGeometry window;
};

// One task's console. The Fields are its widgets; the rest is what the console is a function of
// -- the lines the run produced and the rows its filter lets through. None of it is touched by a
// run's thread: that only ever sends batches down the Channel.
struct TaskPane {
    core::Field<ui::ProgressBar> progress{{.label = "not run yet"}};
    core::Field<ui::Label<>> stats{{"no output yet"}};
    core::Field<ui::TextField<>> filter{{.placeholder = "Filter lines"}};
    core::Field<ui::Checkbox> errors_only{{.label = "Errors and warnings"}};
    core::Field<ui::Button> stop{{.text = "Stop", .primary = false}};
    core::Field<bool> running{false};

    std::vector<runner::ConsoleLine> lines{};   // the run, as it came
    core::List<runner::ConsoleLine> rows{};     // what the console shows: the lines the filter keeps

    std::jthread worker{};
    unsigned launches = 0;
    steady::time_point started{};
    double elapsed_s = 0.0;
    double progress_value = 0.0;
    bool saw_progress = false;
    std::optional<int> exit_code{};
    bool stopped = false;
    std::size_t warnings = 0;
    std::size_t errors = 0;
};

struct TaskRunnerApp {
    TaskRunnerPrefs prefs;

    // Declared before the panes on purpose: a run's thread sends through it, and the panes'
    // threads are joined (in ~TaskPane) before this dies.
    struct Link {
        core::Channel<OutputBatch> output{};
    };
    Link link{};

    // A dock panel's content is built from these, so the set of tasks is what the catalogue says
    // at startup: a run appends to its task's console rather than opening a tab of its own.
    std::array<TaskPane, runner::kTaskCount> panes{};

    core::Field<ui::TabBar<>> tabs{};
    core::List<console_rows::TaskRow> task_rows{};
    core::Field<std::optional<std::size_t>> selected_task{std::optional<std::size_t>{0}};
    core::Field<ui::TextField<>> custom_command{{.placeholder = "a shell command, e.g. git status"}};
    core::Field<ui::Button> run{{.text = "Run", .primary = true,
                                .shortcut = ui::parse_shortcut("Ctrl+Enter").value_or(ui::Shortcut{})}};
    core::Field<ui::Label<>> note{
        {"Build, Unit tests and Flood are generated: the run's own thread writes the same lines "
         "every time, which is how a warning, a failing build and a 200 000 line flood can be "
         "summoned on demand. ping, ls and uname start a real process through /bin/sh -- on the "
         "web there is no shell, so only the generated three are offered there.",
         /*heading=*/false, /*wrap=*/true}};

    core::Field<ui::Label<>> status_runs{{"idle   0 runs"}};
    core::Field<ui::Label<>> status_throughput{{"0 lines/s"}};
    core::Field<ui::Label<>> status_held{{"0 lines held"}};

    std::vector<std::size_t> tab_tasks{};   // tab position -> task
    std::vector<std::size_t> row_tasks{};   // task row -> task
    std::vector<core::Connection> wiring{};

    steady::time_point rate_at_{steady::now()};
    double lines_per_second = 0.0;
    std::size_t rate_lines = 0;
    unsigned runs = 0;

    TaskRunnerApp() {
        for (std::size_t task = 0; task < runner::kTaskCount; ++task) {
            if (!runner::task_available(runner::kTasks[task], runner::kNativeProcesses)) continue;
            tab_tasks.push_back(task);
            row_tasks.push_back(task);
            task_rows.push_back({.text = std::string{runner::kTasks[task].name}});
        }

        for (std::size_t task : tab_tasks) {
            wiring.push_back(
                panes[task].stop.on_change().connect([this, task](const auto&) { stop_task(task); }));
            wiring.push_back(
                panes[task].filter.on_change().connect([this, task](const auto&) { refilter(task); }));
            wiring.push_back(
                panes[task].errors_only.on_change().connect([this, task](const auto&) { refilter(task); }));
        }
        wiring.push_back(run.on_change().connect([this](const auto&) { run_selected(); }));
        wiring.push_back(custom_command.on_change().connect([this](const auto&) { refresh_task_rows(); }));
        wiring.push_back(selected_task.on_change().connect([this](const auto&) { open_selected(); }));
        wiring.push_back(tabs.on_change().connect([this](const ui::TabBar<>& bar) { select_tab(bar.selected); }));

        refresh_task_rows();
    }

    // --- the task list --------------------------------------------------------------------

    console_rows::TaskState state_of(std::size_t task) const {
        const TaskPane& pane = panes[task];
        if (pane.running.get()) return console_rows::TaskState::Running;
        if (!pane.exit_code) return console_rows::TaskState::Idle;
        if (pane.stopped) return console_rows::TaskState::Stopped;
        return *pane.exit_code == 0 ? console_rows::TaskState::Passed : console_rows::TaskState::Failed;
    }

    // The row is a value: when what it says changes, the list is told, and only that row is
    // painted again.
    void refresh_task_rows() {
        for (std::size_t row = 0; row < row_tasks.size(); ++row) {
            const std::size_t task = row_tasks[row];
            const console_rows::TaskRow fresh{
                .text = fmt::format("{} ({})", runner::kTasks[task].name, console_rows::state_label(state_of(task))),
                .state = state_of(task)};
            if (task_rows[row] != fresh) task_rows.set(row, fresh);
        }
    }

    std::size_t row_of(std::size_t task) const {
        const auto it = std::find(row_tasks.begin(), row_tasks.end(), task);
        return it == row_tasks.end() ? 0 : static_cast<std::size_t>(it - row_tasks.begin());
    }

    void open_selected() {
        const auto row = selected_task.get();
        if (!row || *row >= row_tasks.size()) return;
        show_task(row_tasks[*row]);
    }

    void select_tab(std::size_t position) {
        if (position >= tab_tasks.size()) return;
        selected_task.set(row_of(tab_tasks[position]));
    }

    void show_task(std::size_t task) {
        const auto it = std::find(tab_tasks.begin(), tab_tasks.end(), task);
        if (it == tab_tasks.end()) return;
        tabs.set({.selected = static_cast<std::size_t>(it - tab_tasks.begin())});
    }

    // --- starting and stopping runs -------------------------------------------------------

    std::string command_for(std::size_t task) const {
        if (task == runner::kCustomTask) return custom_command.get().value;
        return std::string{runner::kTasks[task].command};
    }

    void run_selected() {
        const auto row = selected_task.get();
        if (!row || *row >= row_tasks.size()) return;
        launch(row_tasks[*row]);
    }

    void launch(std::size_t task) {
        const auto script = runner::kTasks[task].simulated;
        const std::string command = command_for(task);
        if (!script && command.empty()) return explain(task, "type a command in the box by the task list");
        if (!script && !runner::kNativeProcesses)
            return explain(task, "a browser has no shell: this task exists in the desktop build");

        stop_task(task);
        TaskPane& pane = panes[task];
        // One console per task, showing its latest run: starting a task again stops the previous
        // run and starts over, which is also what keeps 200 000 line runs bounded in memory.
        pane.lines.clear();
        pane.rows.replace_all(std::vector<runner::ConsoleLine>{});
        pane.launches += 1;
        pane.started = steady::now();
        pane.elapsed_s = 0.0;
        pane.progress_value = 0.0;
        pane.saw_progress = false;
        pane.exit_code = std::nullopt;
        pane.stopped = false;
        pane.warnings = 0;
        pane.errors = 0;
        pane.running.set(true);
        pane.progress.set({.value = 0.0, .label = "starting", .busy = true});
        ++runs;

        push_line(task, fmt::format("$ {}    (run {})", command, pane.launches), runner::LineKind::Command);
        sync_stats(task);
        refresh_task_rows();

        const unsigned run = pane.launches;
        const auto start = pane.started;
        pane.worker = std::jthread([this, task, run, script, command, start](std::stop_token stop) {
            run_task(task, run, script, command, start, stop);
        });
    }

    // A run the app declines to start: the console says why, in the console's own language.
    void explain(std::size_t task, std::string text) {
        TaskPane& pane = panes[task];
        pane.lines.clear();
        pane.rows.replace_all(std::vector<runner::ConsoleLine>{});
        pane.exit_code = 127;
        pane.stopped = false;
        pane.elapsed_s = 0.0;
        pane.running.set(false);
        pane.progress.set({.value = 0.0, .label = "not started"});
        push_line(task, fmt::format("$ {}", command_for(task)), runner::LineKind::Command);
        push_line(task, std::move(text), runner::LineKind::Error);
        sync_stats(task);
        refresh_task_rows();
        show_task(task);
    }

    // Joining takes as long as the process takes to die, which is why a stop is not "set a flag
    // and hope": the run is being killed, so this is a fraction of a second, not the run's length.
    void stop_task(std::size_t task) {
        TaskPane& pane = panes[task];
        if (!pane.worker.joinable()) return;
        pane.worker.request_stop();
        pane.worker.join();
    }

    // --- the two run kinds, on the run's own thread ---------------------------------------

    // What a run's thread does: the generated script, or the process (desktop only -- launch()
    // never starts the second kind where there is no shell).
    void run_task(std::size_t task, unsigned run, std::optional<runner::SimKind> script,
                  const std::string& command, steady::time_point start, const std::stop_token& stop) {
        if (script) return read_script(task, run, runner::simulated_run(*script, run), start, stop);
#if PRISM_TASK_RUNNER_POSIX
        read_process(task, run, command, stop);
#else
        (void)command;
#endif
    }

    void send_batch(std::size_t task, unsigned run, std::vector<std::string> lines, bool finished,
                    int exit_code, bool stopped = false) {
        link.output.send(OutputBatch{.task = task,
                                     .run = run,
                                     .lines = std::move(lines),
                                     .finished = finished,
                                     .exit_code = exit_code,
                                     .stopped = stopped});
    }

    // A generated run: the script says when each line is due, so a build takes ten seconds and a
    // flood takes what the console takes.
    void read_script(std::size_t task, unsigned run, runner::SimRun script, steady::time_point start,
                     const std::stop_token& stop) {
        std::vector<std::string> batch;
        batch.reserve(std::min(kBatchLines, script.line_count));
        auto last_send = steady::now();
        for (std::size_t i = 0; i < script.line_count; ++i) {
            const double due_s = runner::simulated_time(script, i);
            const auto due = start + std::chrono::duration_cast<steady::duration>(
                                       std::chrono::duration<double>(due_s));
            if (due_s > 0.0 && !wait_until(due, stop))
                return send_batch(task, run, std::move(batch), true, kStoppedCode, /*stopped=*/true);
            batch.push_back(runner::simulated_line(script, i));
            if (batch.size() >= kBatchLines || steady::now() - last_send >= kBatchInterval) {
                send_batch(task, run, std::exchange(batch, {}), false, 0);
                last_send = steady::now();
            }
        }
        send_batch(task, run, std::move(batch), true, script.exit_code);
    }

#if PRISM_TASK_RUNNER_POSIX
    // A real run: bytes off the pipe, cut into lines, batched, and killed if the user says so.
    void read_process(std::size_t task, unsigned run, const std::string& command, const std::stop_token& stop) {
        std::string error;
        auto child = runner::ChildProcess::spawn_shell(command, error);
        if (!child) return send_batch(task, run, {std::move(error)}, true, 127);

        runner::LineAssembler assembler;
        std::vector<std::string> batch;
        std::vector<std::string> decoded;
        char buffer[4096];
        auto last_send = steady::now();
        while (!stop.stop_requested()) {
            const auto got = child->read_some(buffer, sizeof buffer);
            if (got == 0) break;   // the output is over: the child is done
            if (got > 0) {
                decoded.clear();
                runner::decode_chunk(assembler, std::string_view(buffer, static_cast<std::size_t>(got)), decoded);
                for (auto& line : decoded) batch.push_back(std::move(line));
            }
            const bool due = batch.size() >= kBatchLines || steady::now() - last_send >= kBatchInterval;
            if (!batch.empty() && due) {
                last_send = steady::now();
                send_batch(task, run, std::exchange(batch, {}), false, 0);
            }
        }

        if (stop.stop_requested()) {
            const int code = child->stop(std::chrono::milliseconds{200});
            return send_batch(task, run, std::move(batch), true, code, /*stopped=*/true);
        }
        for (auto& line : assembler.finish()) batch.push_back(runner::strip_ansi(line));
        send_batch(task, run, std::move(batch), true, child->wait());
    }
#endif

    // --- what the model does with a delivery ----------------------------------------------

    // Called from drain(), on the logic thread, with the waker having already told the loop there
    // is something to do.
    void drain() { link.output.drain_notifications(); }

    void ingest(const OutputBatch& batch) {
        TaskPane& pane = panes[batch.task];
        if (batch.run != pane.launches) return;   // a stopped run's leftovers

        append(batch.task, batch.lines);
        if (batch.finished) ended(batch.task, batch.exit_code, batch.stopped);
        else sync_progress(batch.task);
        sync_stats(batch.task);
        refresh_task_rows();
    }

    void append(std::size_t task, const std::vector<std::string>& texts) {
        TaskPane& pane = panes[task];
        const auto filter = filter_of(task);   // one copy of the needle for the whole batch
        for (const auto& text : texts) {
            const runner::ConsoleLine line{text, runner::classify_line(text)};
            pane.lines.push_back(line);
            if (filter.keeps(line.text, line.kind)) pane.rows.push_back(line);
            if (line.kind == runner::LineKind::Error) ++pane.errors;
            if (line.kind == runner::LineKind::Warning) ++pane.warnings;
            if (auto done = runner::parse_progress(line.text)) {
                pane.progress_value = *done;
                pane.saw_progress = true;
            }
        }
    }

    // One line, for what the model itself says: the command header, why a run cannot start, and
    // how the run ended.
    void push_line(std::size_t task, std::string text, runner::LineKind kind) {
        TaskPane& pane = panes[task];
        pane.lines.push_back({text, kind});
        if (filter_of(task).keeps(text, kind)) pane.rows.push_back({std::move(text), kind});
    }

    void ended(std::size_t task, int code, bool stopped) {
        TaskPane& pane = panes[task];
        pane.elapsed_s = elapsed_of(task);
        pane.running.set(false);
        pane.exit_code = code;
        pane.stopped = stopped;
        pane.progress.set({.value = final_progress(task), .label = outcome_label(task)});
        push_line(task, fmt::format("exit {}    {} lines in {:.1f} s", code, grouped(pane.lines.size()), pane.elapsed_s),
                  code == 0 ? runner::LineKind::Success : runner::LineKind::Error);
        sync_stats(task);
        refresh_task_rows();
    }

    double elapsed_of(std::size_t task) const {
        const TaskPane& pane = panes[task];
        if (!pane.running.get()) return pane.elapsed_s;
        return std::chrono::duration<double>(steady::now() - pane.started).count();
    }

    double final_progress(std::size_t task) const {
        const TaskPane& pane = panes[task];
        if (pane.saw_progress) return pane.progress_value;
        return pane.exit_code.value_or(1) == 0 ? 1.0 : 0.0;
    }

    std::string outcome_label(std::size_t task) const {
        const TaskPane& pane = panes[task];
        if (pane.stopped) return fmt::format("stopped after {:.1f} s", pane.elapsed_s);
        if (!pane.exit_code || *pane.exit_code != 0)
            return fmt::format("failed (exit {}) after {:.1f} s", pane.exit_code.value_or(-1), pane.elapsed_s);
        return fmt::format("done in {:.1f} s", pane.elapsed_s);
    }

    void sync_progress(std::size_t task) {
        TaskPane& pane = panes[task];
        if (!pane.running.get()) return;
        const double seconds = elapsed_of(task);
        if (!pane.saw_progress) {
            pane.progress.set({.value = 0.0,
                               .label = fmt::format("{} lines    {:.1f} s", grouped(pane.lines.size()), seconds),
                               .busy = true});
            return;
        }
        pane.progress.set({.value = pane.progress_value,
                           .label = fmt::format("{:.0f}%    {} lines    {:.1f} s", pane.progress_value * 100.0,
                                                grouped(pane.lines.size()), seconds)});
    }

    void sync_stats(std::size_t task) {
        TaskPane& pane = panes[task];
        const std::size_t total = pane.lines.size();
        if (total == 0) return pane.stats.set({"no output yet"});
        const double seconds = elapsed_of(task);
        const double rate = seconds > 0.0 ? static_cast<double>(total) / seconds : 0.0;
        std::string problems = (pane.warnings || pane.errors)
            ? fmt::format("    {} warnings, {} error{}", pane.warnings, pane.errors, pane.errors == 1 ? "" : "s")
            : std::string{};
        std::string filtered = pane.rows.size() == total
            ? std::string{}
            : fmt::format("    {} shown", grouped(pane.rows.size()));
        pane.stats.set({fmt::format("{} lines{}{}    {:.0f} lines/s    {:.1f} s", grouped(total), problems,
                                    filtered, rate, seconds)});
    }

    runner::LineFilter filter_of(std::size_t task) const {
        return {.needle = panes[task].filter.get().value,
                .errors_only = panes[task].errors_only.get().checked};
    }

    // Filtering is a scan of what the console holds: a few milliseconds at 200 000 lines, on a
    // keystroke. Incremental filtering would be the next step, and is not needed at this size.
    void refilter(std::size_t task) {
        TaskPane& pane = panes[task];
        pane.rows.replace_all(runner::filtered_lines(pane.lines, filter_of(task)));
        sync_stats(task);
    }

    // --- per frame ------------------------------------------------------------------------

    void on_frame() {
        for (std::size_t task : tab_tasks) {
            if (!panes[task].running.get()) continue;
            sync_progress(task);
            sync_stats(task);
        }
        update_rates(steady::now());
    }

    // What the consoles hold between them: the number that says what a 200 000 line run costs.
    std::size_t lines_held() const {
        std::size_t held = 0;
        for (std::size_t task : tab_tasks) held += panes[task].lines.size();
        return held;
    }

    void update_rates(steady::time_point now) {
        const std::size_t held = lines_held();
        unsigned running = 0;
        for (std::size_t task : tab_tasks) running += panes[task].running.get() ? 1u : 0u;
        const double dt = std::chrono::duration<double>(now - rate_at_).count();
        if (dt >= 0.25) {
            // Signed: a relaunch empties a console, and the count must not wrap around.
            const double fresh = (static_cast<double>(held) - static_cast<double>(rate_lines)) / dt;
            lines_per_second = lines_per_second * 0.5 + fresh * 0.5;
            rate_lines = held;
            rate_at_ = now;
        }
        status_runs.set({running == 0 ? fmt::format("idle    {} runs", runs)
                                      : fmt::format("{} running    {} runs", running, runs)});
        const auto rate = static_cast<std::size_t>(std::lround(std::max(0.0, lines_per_second)));
        status_throughput.set({fmt::format("{} lines/s", grouped(rate))});
        status_held.set({fmt::format("{} lines held", grouped(held))});
    }

    // --- the view -------------------------------------------------------------------------

    void pane_console(app::WidgetTree::ViewBuilder& p, std::size_t task) {
        TaskPane& pane = panes[task];
        p.vstack([&] {
            p.hstack([&] {
                p.widget(pane.filter).tooltip("Show only the lines that contain this text");
                p.widget(pane.errors_only).tooltip("Hide everything that is not an error or a warning");
                p.spacer();
                p.widget(pane.stop).tooltip("Stop the run: SIGTERM, then SIGKILL").enabled(pane.running);
            });
            p.widget(pane.progress);
            p.widget(pane.stats);
            p.list(pane.rows).follow_tail();   // Widget<ConsoleLine>: a row's look is a specialization
        });
    }

    void tasks_panel(app::WidgetTree::ViewBuilder& p) {
        p.vstack([&] {
            std::move(p.list(task_rows)).selection(selected_task);
            p.widget(custom_command).tooltip("What the Custom task runs, when it is the selected one");
            p.widget(run).tooltip("Run the selected task (Ctrl+Enter)");
            p.widget(note);
        });
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(prefs.layout, {
                {"tasks", "Tasks", [&](app::WidgetTree::ViewBuilder& p) { tasks_panel(p); }},
                {"console", "Console", [&](app::WidgetTree::ViewBuilder& p) {
                    p.tabs(tabs, [&] {
                        for (std::size_t task : tab_tasks) {
                            p.tab(std::string{runner::kTasks[task].name},
                                  [this, task](app::WidgetTree::ViewBuilder& q) { pane_console(q, task); });
                        }
                    });
                }},
            });
            vb.status_bar([&] {
                vb.widget(status_runs);
                vb.spacer();
                vb.widget(status_throughput);
                vb.gap(16.f);
                vb.widget(status_held);
            });
        });
    }

    // --- the frame the capture shows ------------------------------------------------------

    // What the screenshot is of: a finished Build (every colour a console has, an exit code, a
    // progress bar that knows how far it got) and a Flood still running, so the task list and the
    // status bar have something to say. Nothing here waits on a clock: a headless capture never
    // ticks one, and a thread would be a thread running during a screenshot.
    void seed_demo_data() {
        seed_build_capture();
        seed_flood_capture();
        show_task(runner::kBuildTask);
        selected_task.set(row_of(runner::kBuildTask));
        status_runs.set({"1 running    2 runs"});
        status_throughput.set({fmt::format("{} lines/s", grouped(48'200))});
        status_held.set({fmt::format("{} lines held", grouped(lines_held()))});
        refresh_task_rows();
    }

    void seed_build_capture() {
        const std::size_t task = runner::kBuildTask;
        TaskPane& pane = panes[task];
        const auto script = runner::simulated_build(1);
        pane.launches = 1;
        pane.started = steady::now() - std::chrono::milliseconds{static_cast<int>(script.duration_s * 1000.0)};
        pane.running.set(true);   // so the run's own elapsed time is what the exit line reports
        push_line(task, fmt::format("$ {}", command_for(task)), runner::LineKind::Command);
        append(task, script_lines(script, script.line_count));
        ended(task, script.exit_code, false);
    }

    void seed_flood_capture() {
        const std::size_t task = runner::kFloodTask;
        TaskPane& pane = panes[task];
        const auto script = runner::simulated_flood();
        pane.launches = 1;
        pane.started = steady::now() - std::chrono::milliseconds{1400};
        push_line(task, fmt::format("$ {}", command_for(task)), runner::LineKind::Command);
        append(task, script_lines(script, script.line_count * 3 / 10));
        pane.running.set(true);
        sync_progress(task);
        sync_stats(task);
    }

    static std::vector<std::string> script_lines(const runner::SimRun& script, std::size_t count) {
        std::vector<std::string> texts;
        texts.reserve(count);
        for (std::size_t i = 0; i < count; ++i) texts.push_back(runner::simulated_line(script, i));
        return texts;
    }
};

int main(int argc, char* argv[]) {
    TaskRunnerApp app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1000, 700);
    }

    app::SettingsFile settings{app.prefs, backends::settings_dir("prism", "task-runner") / "settings.toml"};

    app::model_app(app.prefs.window.applied_to({.title = "PRISM Task Runner", .width = 1000, .height = 700,
                                                .decoration = app::DecorationMode::Custom}),
                   app, [&app](app::AppContext& ctx) {
        ctx.remember_window_size(app.prefs.window);

        // Every run's thread reports through this one Channel; the model drains it on the logic
        // thread, which is where the console, the progress bar and the counters are written.
        app.link.output.set_waker(ctx.waker());
        app.link.output.observe([&app](const OutputBatch& batch) { app.ingest(batch); });

        ctx.clock().add([&app](auto) {
            app.on_frame();
            return true;
        });

        // The window opens with a build already running: nothing it does blocks the frame that
        // shows it starting.
        app.launch(runner::kBuildTask);
    });

    return 0;
}
