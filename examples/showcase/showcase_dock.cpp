#include "showcase_common.hpp"
#include <prism/ui/dock.hpp>

using namespace prism;

struct DockShowcase {
    core::List<std::string> files;
    core::Field<ui::TextArea<>> notes{{.value = "Drag a tab to move its panel.", .rows = 4}};
    core::Field<ui::Label<>> log{{"Ready."}};
    core::Field<ui::DockLayout> layout{{ui::dock_split(/*vertical=*/false, {
        ui::dock_group({"files"}),
        ui::dock_split(/*vertical=*/true, {ui::dock_group({"notes"}), ui::dock_group({"log"})}),
    })}};

    DockShowcase() {
        for (auto f : {"main.cpp", "app.hpp", "README.md"}) files.push_back(f);
    }

    void view(app::ViewBuilder& vb) {
        vb.dock(layout, {
            {"files", "Files", [&](app::ViewBuilder& p) { p.list(files); }},
            {"notes", "Notes", [&](app::ViewBuilder& p) { p.widget(notes); }},
            {"log", "Log", [&](app::ViewBuilder& p) { p.widget(log); }, /*closable=*/true},
        });
    }
};

int main(int argc, char* argv[]) {
    DockShowcase model;
    return showcase(argc, argv, model, 560, 280);
}
