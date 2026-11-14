// Docking: four small panels in a dock (doc/design/drag-and-docking.md). The user drags a tab
// onto another panel's middle to add it to its tabs, onto an edge to split there, or out of the
// window to float it. The arrangement is the gallery's preference, saved with the settings.
#pragma once

#include <prism/prism.hpp>
#include <prism/ui/dock.hpp>

#include <fmt/format.h>

#include <optional>
#include <string>
#include <vector>

namespace gallery {

struct DockingTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Docking", .heading = true}};
    prism::core::Field<prism::ui::Label<>> hint{
        {.value = "Drag a panel's tab onto another panel: the guide's buttons put it in that "
                  "panel's tabs, beside it, or along a whole side of the dock; let it go outside "
                  "the window to float it.",
         .wrap = true}};
    prism::core::Field<prism::ui::TextArea<>> notes{{.value = "Type here: the text moves with its panel.", .rows = 4}};
    prism::core::List<std::string> items;
    prism::core::Field<std::optional<std::size_t>> picked{std::nullopt};
    prism::core::Field<prism::ui::Checkbox> wrap{{.checked = true, .label = "Wrap"}};
    prism::core::Field<prism::ui::SpinBox<int>> copies{{.value = 2, .min = 1, .max = 9, .step = 1}};
    prism::core::Field<prism::ui::Label<>> log{{"Waiting for a click."}};
    prism::core::Field<prism::ui::DockLayout>& layout;   // the gallery's preference, arranged here
    std::vector<prism::core::Connection> wiring;         // captures `this`: the tab never moves

    explicit DockingTab(prism::core::Field<prism::ui::DockLayout>& arrangement) : layout(arrangement) {
        for (auto item : {"Notes", "Table", "Plot"}) items.push_back(item);
        wiring.push_back(wrap.on_change().connect([this](const prism::ui::Checkbox& b) {
            log.set({b.checked ? "Wrap is on." : "Wrap is off."});
        }));
        wiring.push_back(copies.on_change().connect([this](const prism::ui::SpinBox<int>& s) {
            log.set({fmt::format("{} copies.", s.value)});
        }));
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.widget(hint);
        vb.dock(layout, {
            {"notes", "Notes", [&](auto& p) { p.widget(notes); }},
            {"list", "List", [&](auto& p) { p.list(items).selection(picked); }},
            {"controls", "Controls", [&](auto& p) { p.vstack([&] { p.widget(wrap); p.widget(copies); }); }},
            {"log", "Log", [&](auto& p) { p.widget(log); }},
        });
    }
};

} // namespace gallery
