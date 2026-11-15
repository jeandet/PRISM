#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>
#include "blocks/console_panel.hpp"
#include "blocks/status_bar.hpp"
#include "blocks/tasks_panel.hpp"
#include "process/clock.hpp"
#include "process/run_job.hpp"
#include "../showcase/showcase_common.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace prism;

// Glaze reflects this struct: every member is a key, a nested struct a table. Keep it out of an
// anonymous namespace (Glaze needs a type with linkage).
struct TaskRunnerPrefs {
    core::Field<ui::DockLayout> layout{{ui::dock_split(/*vertical=*/false, {
        ui::dock_group({"tasks"}),
        ui::dock_group({"console"})
    }, {0.30f, 0.70f})}};
    app::WindowGeometry window;
};

// The app is the composition: three blocks, the channel the runs report through, and the wiring
// between them. What each block is lives in `blocks/`; what a run does on its own thread, and
// what a console can read out of the output, lives in `process/` and has no PRISM in it.
struct TaskRunnerApp {
    TaskRunnerPrefs prefs;

    // Declared before the blocks on purpose: a run's thread sends through it, and the blocks'
    // threads are joined (in ~ConsolePane) before this dies.
    struct Link {
        core::Channel<runner::OutputBatch> output{};
    };
    Link link{};

    runner::blocks::TasksPanel tasks{};
    runner::blocks::ConsolePanel consoles{};
    runner::blocks::StatusBar status{};

    std::vector<core::Connection> wiring{};

    TaskRunnerApp() {
        wiring.push_back(tasks.run.on_change().connect([this](const auto&) { run_selected(); }));
        wiring.push_back(tasks.selected.on_change().connect([this](const auto&) { open_selected(); }));
        wiring.push_back(consoles.tabs.on_change().connect([this](const ui::TabBar<>& bar) {
            if (const auto task = consoles.task_of_tab(bar.selected)) tasks.select_task(*task);
        }));
    }

    // --- starting and stopping runs -------------------------------------------------------

    std::string command_for(std::size_t task) const {
        if (task == runner::kCustomTask) return tasks.custom_command.get().value;
        return std::string{runner::kTasks[task].command};
    }

    void run_selected() {
        if (const auto task = tasks.selected_task()) launch(*task);
    }

    void open_selected() { consoles.show(tasks.selected_task()); }

    void launch(std::size_t task) {
        const auto script = runner::kTasks[task].simulated;
        const std::string command = command_for(task);
        if (!script && command.empty()) return explain(task, "type a command in the box by the task list");
        if (!script && !runner::kNativeProcesses)
            return explain(task, "a browser has no shell: this task exists in the desktop build");

        // Every run's thread reports through this one Channel, which is the only thing a run's
        // thread can do to a console: it never touches a Field.
        consoles.pane(task).launch(task, script, command,
                                   [this](runner::OutputBatch batch) { link.output.send(std::move(batch)); });
    }

    void explain(std::size_t task, std::string text) {
        consoles.pane(task).refuse(command_for(task), std::move(text));
        refresh_rows();
        consoles.show(task);
    }

    // --- what the model does with a delivery ----------------------------------------------

    void ingest(const runner::OutputBatch& batch) {
        if (consoles.pane(batch.task).ingest(batch)) refresh_rows();
    }

    // --- per frame ------------------------------------------------------------------------

    void on_frame() {
        for (std::size_t task : consoles.tab_tasks) consoles.pane(task).tick();
        refresh_rows();
        status.refresh(runner::steady::now(), consoles.running(), consoles.launches(), consoles.lines());
    }

    void refresh_rows() { tasks.refresh([this](std::size_t task) { return consoles.pane(task).state(); }); }

    // --- the view -------------------------------------------------------------------------

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(prefs.layout, {
                {"tasks", "Tasks", [&](app::WidgetTree::ViewBuilder& p) { p.block(tasks); }},
                {"console", "Console", [&](app::WidgetTree::ViewBuilder& p) { p.block(consoles); }},
            });
            vb.status_bar([&] { status.view(vb); });
        });
    }

    // --- the frame the capture shows ------------------------------------------------------

    // What the screenshot is of: a finished Build and a Flood still running, so the task list and
    // the status bar have something to say.
    void seed_demo_data() {
        consoles.pane(runner::kBuildTask)
            .seed_finished(runner::simulated_build(1), command_for(runner::kBuildTask));
        consoles.pane(runner::kFloodTask)
            .seed_running(runner::simulated_flood(), command_for(runner::kFloodTask),
                          runner::kFloodLines * 3 / 10, /*back_s=*/1.4);
        consoles.show(runner::kBuildTask);
        tasks.select_task(runner::kBuildTask);
        status.show(1, 2, consoles.lines(), 48'200);
        refresh_rows();
    }
};

int main(int argc, char* argv[]) {
    TaskRunnerApp app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1000, 700);
    }

    app::run(app, {.title = "PRISM Task Runner", .width = 1000, .height = 700,
                   .settings = &app.prefs, .organization = "prism", .name = "task-runner"},
             [&app](app::AppContext& ctx) {
        ctx.receive(app.link.output, [&app](const runner::OutputBatch& batch) { app.ingest(batch); });

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
