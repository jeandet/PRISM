// Widget Gallery: every PRISM widget in one window, in five tabs, under a menu bar with Alt
// shortcuts. The tabs are plain model structs (controls_tab.hpp, ...); this file arranges them.
#include "controls_tab.hpp"
#include "data_tab.hpp"
#include "plot_tab.hpp"
#include "popups_tab.hpp"
#include "text_tab.hpp"
#include "../showcase/showcase_common.hpp"

#include <array>
#include <string>
#include <vector>

using namespace prism;

struct Gallery {
    static constexpr std::array<const char*, 5> tab_names{"Controls", "Text", "Data", "Plot", "Popups"};

    core::Field<ui::Menu> file_menu{{.title = "&File", .items = {"&Open…", "&Save as…"}}};
    core::Field<ui::Menu> view_menu{{.title = "&View", .items = {"&Controls", "&Text", "&Data", "&Plot", "P&opups"}}};
    core::Field<ui::Menu> help_menu{{.title = "&Help", .items = {"&About PRISM"}}};
    core::Field<ui::Dialog> about{{.title = "PRISM Widget Gallery",
                                   .message = "A C++26 GUI framework: model structs, persistent widgets, "
                                              "reactive fields. This page is the same C++ as the desktop "
                                              "build, compiled to WebAssembly.",
                                   .buttons = {"Close"}}};
    core::Field<ui::TabBar<>> tabs;
    gallery::ControlsTab controls;
    gallery::TextTab text;
    gallery::DataTab data;
    gallery::PlotTab plot;
    gallery::PopupsTab popups;
    std::vector<core::Connection> wiring;   // captures `this`: the gallery never moves

    Gallery() {
        wiring.push_back(file_menu.on_change().connect([this](const ui::Menu& m) {
            if (m.chosen) popups.choose(*m.chosen == 0 ? app::FileDialogKind::Open : app::FileDialogKind::Save);
        }));
        wiring.push_back(view_menu.on_change().connect([this](const ui::Menu& m) {
            if (m.chosen) tabs.set({.selected = *m.chosen});
        }));
        wiring.push_back(help_menu.on_change().connect([this](const ui::Menu& m) {
            if (m.chosen) about.set({.title = about.get().title, .message = about.get().message,
                                     .buttons = about.get().buttons, .open = true});
        }));
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(file_menu);
            vb.widget(view_menu);
            vb.widget(help_menu);
            vb.spacer();
        });
        vb.tabs(tabs, [&] {
            vb.tab(tab_names[0], [&](app::WidgetTree::ViewBuilder& t) { t.scroll([&] { t.component(controls); }); });
            vb.tab(tab_names[1], [&](app::WidgetTree::ViewBuilder& t) { t.scroll([&] { t.component(text); }); });
            vb.tab(tab_names[2], [&](app::WidgetTree::ViewBuilder& t) { t.component(data); });
            vb.tab(tab_names[3], [&](app::WidgetTree::ViewBuilder& t) { t.component(plot); });
            vb.tab(tab_names[4], [&](app::WidgetTree::ViewBuilder& t) { t.component(popups); });
        });
        vb.dialog(about);
    }
};

int main(int argc, char* argv[]) {
    Gallery app;
    if (argc >= 2) {   // README capture: widget_gallery out.svg [tab number]
        if (argc >= 3) app.tabs.set({.selected = std::stoul(argv[2])});
        return showcase(argc, argv, app, 900, 640);
    }
    auto backend = app::Backend::software(app::RenderConfig{});
    auto& window = backend.create_window({.title = "PRISM Widget Gallery", .width = 1000, .height = 700,
                                          .decoration = app::DecorationMode::Custom});
    app::model_app(backend, window, app, [&app](app::AppContext& ctx) {
        app.popups.ctx = &ctx;
        ctx.on_file_drop([&app](const input::FileDrop& drop) { app.popups.dropped(drop); });
    });
    app.popups.ctx = nullptr;   // the context ended with model_app
}
