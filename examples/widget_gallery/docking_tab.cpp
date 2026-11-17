#include "docking_tab.hpp"

#include <fmt/format.h>

namespace gallery {

DockingTab::DockingTab(prism::core::Field<prism::ui::DockLayout>& arrangement) : layout(arrangement) {
    for (auto item : {"Notes", "Table", "Plot"}) items.push_back(item);
    wire.on(wrap, [this](const prism::ui::Checkbox& b) {
        log.set({b.checked ? "Wrap is on." : "Wrap is off."});
    });
    wire.on(copies, [this](const prism::ui::SpinBox<int>& s) {
        log.set({fmt::format("{} copies.", s.value)});
    });
    wire.on(reopen, [this](const prism::ui::Button&) {
        auto l = layout.get();
        const auto closed = l.closed;
        for (const auto& name : closed) l = prism::ui::show_panel(std::move(l), name);
        layout.set(std::move(l));
    });
}

void DockingTab::view(prism::app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(hint);
    vb.widget(reopen);
    vb.dock(layout, {
        {"notes", "Notes", [&](auto& p) { p.widget(notes); }},
        {"list", "List", [&](auto& p) { p.list(items).selection(picked); }, true},
        {"controls", "Controls", [&](auto& p) { p.vstack([&] { p.widget(wrap); p.widget(copies); }); }},
        {"log", "Log", [&](auto& p) { p.widget(log); }, true},
    });
}

} // namespace gallery
