#pragma once

// The task list: what the catalogue offers, what state each task is in, and the box that says
// what Custom runs.

#include <prism/prism.hpp>

#include "../process/catalog.hpp"
#include "console_rows.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace prism::runner::blocks {

struct TasksPanel {
    core::List<console_rows::TaskRow> rows{};
    core::Field<std::optional<std::size_t>> selected{std::optional<std::size_t>{0}};
    core::Field<ui::TextField<>> custom_command{{.placeholder = "a shell command, e.g. git status"}};
    core::Field<ui::Button> run{{.text = "Run", .primary = true,
                                .shortcut = ui::parse_shortcut("Ctrl+Enter").value_or(ui::Shortcut{})}};
    core::Field<ui::Label<>> note{
        {"Build, Unit tests and Flood are generated: the run's own thread writes the same lines "
         "every time, which is how a warning, a failing build and a 200 000 line flood can be "
         "summoned on demand. ping, ls and uname start a real process through /bin/sh -- on the "
         "web there is no shell, so only the generated three are offered there.",
         /*heading=*/false, /*wrap=*/true}};

    std::vector<std::size_t> task_ids{};   // row -> task

    TasksPanel() : task_ids{offered_tasks()} {
        for (std::size_t task : task_ids) rows.push_back({.text = std::string{kTasks[task].name}});
    }

    std::optional<std::size_t> task_of_row(std::size_t row) const {
        return row < task_ids.size() ? std::optional{task_ids[row]} : std::nullopt;
    }

    std::optional<std::size_t> selected_task() const {
        const auto row = selected.get();
        return row ? task_of_row(*row) : std::nullopt;
    }

    void select_task(std::size_t task) {
        if (const auto row = row_of(task)) selected.set(*row);
    }

    // The row is a value: when what it says changes, the list is told, and only that row is
    // painted again.
    void set_state(std::size_t task, console_rows::TaskState state) {
        const auto row = row_of(task);
        if (!row) return;
        const console_rows::TaskRow fresh{.text = row_text(task, state), .state = state};
        if (rows[*row] != fresh) rows.set(*row, fresh);
    }

    // What state every task is in, asked of whoever knows: the panel does not own the runners.
    template <class StateOf> void refresh(StateOf&& state_of) {
        for (std::size_t task : task_ids) set_state(task, state_of(task));
    }

    void view(app::ViewBuilder& p) {
        p.vstack([&] {
            std::move(p.list(rows)).selection(selected);
            p.widget(custom_command).tooltip("What the Custom task runs, when it is the selected one");
            p.widget(run).tooltip("Run the selected task (Ctrl+Enter)");
            p.widget(note);
        });
    }

private:
    std::optional<std::size_t> row_of(std::size_t task) const {
        const auto it = std::find(task_ids.begin(), task_ids.end(), task);
        if (it == task_ids.end()) return std::nullopt;
        return static_cast<std::size_t>(it - task_ids.begin());
    }

    static std::string row_text(std::size_t task, console_rows::TaskState state) {
        return fmt::format("{} ({})", kTasks[task].name, console_rows::state_label(state));
    }
};

} // namespace prism::runner::blocks
