// Widget Gallery: every PRISM widget in one window, in five tabs, under a menu bar with Alt
// shortcuts. The tabs are plain model structs (controls_tab.hpp, ...); this file arranges them.
#include "catalog_tab.hpp"
#include "controls_tab.hpp"
#include "data_tab.hpp"
#include "plot_tab.hpp"
#include "popups_tab.hpp"
#include "text_tab.hpp"
#include "../showcase/showcase_common.hpp"

#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>

#include <array>
#include <string>
#include <vector>

using namespace prism;

struct Gallery {
    static constexpr std::array<const char*, 6> tab_names{"Controls", "Catalog", "Text", "Data", "Plot", "Popups"};

    core::Field<ui::Menu> file_menu{{.title = "&File", .items = {
        {.text = "&Open…", .shortcut = {ui::keys::o, ui::mods::ctrl}, .icon = gallery::icon("folder.svg")},
        {.text = "&Save as…", .shortcut = {ui::keys::s, ui::mods::ctrl | ui::mods::shift}, .icon = gallery::icon("save.svg")},
    }}};
    core::Field<ui::Menu> view_menu{{.title = "&View", .items = {{"&Controls"}, {"C&atalog"}, {"&Text"}, {"&Data"}, {"&Plot"}, {"P&opups"}}}};
    core::Field<ui::Menu> help_menu{{.title = "&Help", .items = {{"&About PRISM"}}}};
    core::Field<ui::Dialog> about{{.title = "PRISM Widget Gallery",
                                   .message = "A C++26 GUI framework: model structs, persistent widgets, "
                                              "reactive fields. This page is the same C++ as the desktop "
                                              "build, compiled to WebAssembly.",
                                   .buttons = {"Close"}}};
    core::Field<ui::TabBar<>> tabs;
    gallery::Prefs prefs;
    gallery::ControlsTab controls{prefs.theme};
    gallery::CatalogTab catalog;
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
            vb.tab(tab_names[1], [&](app::WidgetTree::ViewBuilder& t) { t.component(catalog); });
            vb.tab(tab_names[2], [&](app::WidgetTree::ViewBuilder& t) { t.scroll([&] { t.component(text); }); });
            vb.tab(tab_names[3], [&](app::WidgetTree::ViewBuilder& t) { t.component(data); });
            vb.tab(tab_names[4], [&](app::WidgetTree::ViewBuilder& t) { t.component(plot); });
            vb.tab(tab_names[5], [&](app::WidgetTree::ViewBuilder& t) { t.component(popups); });
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
    auto& window = backend.create_window(app.prefs.window.applied_to(
        {.title = "PRISM Widget Gallery", .width = 1000, .height = 700, .decoration = app::DecorationMode::Custom}));
    // Loads the last session's preferences, and saves them as they change.
    app::SettingsFile settings{app.prefs, backends::settings_dir("prism", "widget-gallery") / "settings.toml"};
    core::Connection theme_link;   // follows the Theme dropdown; ends with model_app
    app::model_app(backend, window, app, [&app, &theme_link](app::AppContext& ctx) {
        app.popups.ctx = &ctx;
        ctx.on_file_drop([&app](const input::FileDrop& drop) { app.popups.dropped(drop); });
        auto apply = [&ctx](gallery::Theme t) {
            if (t == gallery::Theme::System) ctx.follow_system_theme();
            else ctx.set_theme(t == gallery::Theme::Light ? ui::light_theme() : ui::dark_theme());
        };
        apply(app.prefs.theme.get());
        ctx.remember_window_size(app.prefs.window);
        theme_link = app.prefs.theme.on_change().connect(apply);
    });
    theme_link.disconnect();
    app.popups.ctx = nullptr;   // the context ended with model_app
}
