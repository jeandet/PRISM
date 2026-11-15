#include "data_tab.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace gallery {

DataTab::DataTab() {
    for (auto s : {"Try sorting the table", "Drag the handle between the panes", "Add an item here",
                   "Drag these rows to reorder them"})
        todo.push_back(s);
    wiring.push_back(add.on_change().connect([this](const auto&) {
        if (new_item.get().value.empty()) return;
        todo.push_back(new_item.get().value);
        new_item.set({.placeholder = new_item.get().placeholder});
    }));
    wiring.push_back(remove.on_change().connect([this](const auto&) {
        if (todo.size() > 0) todo.erase(todo.size() - 1);
    }));
    wiring.push_back(column_menu.on_change().connect([this](const prism::ui::ContextMenu& m) {
        if (m.activations == menu_seen) return;
        menu_seen = m.activations;
        show_ticked(m);
    }));
}

void DataTab::view(prism::app::WidgetTree::ViewBuilder& vb) {
    vb.widget(title);
    vb.hstack([&] {
        vb.vstack([&] {
            vb.table(readings).sort(3, /*descending=*/true).columns(columns);
            vb.context_menu(column_menu);
        });
        vb.handle();
        vb.vstack([&] {
            vb.hstack([&] {
                vb.widget(new_item);
                vb.gap(8.f);
                vb.widget(add);
                vb.gap(8.f);
                vb.widget(remove);
            });
            vb.list(todo).reorderable();
        });
    });
}

// The ticked columns, in the order they show now; a newly ticked one goes last. The last
// column cannot be hidden (an empty list means "all of them").
void DataTab::show_ticked(prism::ui::ContextMenu m) {
    auto ticked = [&](std::size_t c) { return m.items[c].checked.value_or(false); };
    std::vector<std::size_t> order = columns.get();
    if (order.empty())
        for (std::size_t c = 0; c < m.items.size(); ++c) order.push_back(c);
    std::vector<std::size_t> shown;
    for (std::size_t c : order)
        if (ticked(c)) shown.push_back(c);
    for (std::size_t c = 0; c < m.items.size(); ++c)
        if (ticked(c) && std::ranges::find(shown, c) == shown.end()) shown.push_back(c);
    if (shown.empty()) {
        m.items[m.chosen.value_or(0)].checked = true;
        column_menu.set(m);
        return;
    }
    columns.set(std::move(shown));
}

} // namespace gallery
