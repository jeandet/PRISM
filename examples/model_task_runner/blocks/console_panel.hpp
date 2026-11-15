#pragma once

// The console dock: one tab per task the catalogue offers, each tab one `ConsolePane`.

#include <prism/prism.hpp>

#include "../process/catalog.hpp"
#include "console_pane.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace prism::runner::blocks {

struct ConsolePanel {
    core::Field<ui::TabBar<>> tabs{};
    std::vector<std::size_t> tab_tasks{};   // tab position -> task
    std::array<ConsolePane, kTaskCount> panes{};

    ConsolePanel() : tab_tasks{offered_tasks()} {}

    ConsolePane& pane(std::size_t task) { return panes[task]; }
    const ConsolePane& pane(std::size_t task) const { return panes[task]; }

    // Shows `task`'s tab, if this build offers it.
    void show(std::optional<std::size_t> task) {
        if (!task) return;
        const auto it = std::find(tab_tasks.begin(), tab_tasks.end(), *task);
        if (it == tab_tasks.end()) return;
        tabs.set({.selected = static_cast<std::size_t>(it - tab_tasks.begin())});
    }

    std::optional<std::size_t> task_of_tab(std::size_t position) const {
        return position < tab_tasks.size() ? std::optional{tab_tasks[position]} : std::nullopt;
    }

    unsigned running() const {
        unsigned count = 0;
        for (std::size_t task : tab_tasks) count += pane(task).is_running() ? 1u : 0u;
        return count;
    }

    unsigned launches() const {
        unsigned count = 0;
        for (std::size_t task : tab_tasks) count += pane(task).launches();
        return count;
    }

    std::size_t lines() const {
        std::size_t count = 0;
        for (std::size_t task : tab_tasks) count += pane(task).lines();
        return count;
    }

    void view(app::WidgetTree::ViewBuilder& p) {
        p.tabs(tabs, [&] {
            for (std::size_t task : tab_tasks)
                p.tab(std::string{kTasks[task].name},
                      [this, task](app::WidgetTree::ViewBuilder& q) { q.block(panes[task]); });
        });
    }
};

} // namespace prism::runner::blocks
