// Widget Gallery: every PRISM widget in one window, in eight tabs, under a menu bar with Alt
// shortcuts. The tabs are plain model structs (controls_tab.hpp, ...); this file arranges them.
#include "build_your_own_tab.hpp"
#include "catalog_tab.hpp"
#include "controls_tab.hpp"
#include "data_tab.hpp"
#include "docking_tab.hpp"
#include "plot_tab.hpp"
#include "popups_tab.hpp"
#include "text_tab.hpp"
#include "../showcase/showcase_common.hpp"

#include <prism/app/settings.hpp>

#include <array>
#include <string>
#include <vector>

using namespace prism;

struct Gallery {
    static constexpr std::array<const char*, 8> tab_names{
        "Controls", "Catalog", "Text", "Data", "Plot", "Popups", "Docking", "Build your own"};

    core::Field<ui::Menu> file_menu{{.title = "&File", .items = {
        {.text = "&Open…", .shortcut = {ui::keys::o, ui::mods::ctrl}, .icon = gallery::icon("folder.svg")},
        {.text = "&Save as…", .shortcut = {ui::keys::s, ui::mods::ctrl | ui::mods::shift}, .icon = gallery::icon("save.svg")},
    }}};
    core::Field<ui::Menu> view_menu{{.title = "&View", .items = {{"&Controls"}, {"C&atalog"}, {"&Text"}, {"&Data"}, {"&Plot"}, {"P&opups"}, {"Doc&king"}, {"&Build your own"}}}};
    core::Field<ui::Menu> help_menu{{.title = "&Help", .items = {{"&About PRISM"}}}};
    core::Field<ui::Dialog> about{{.title = "PRISM Widget Gallery",
                                   .message = "A C++26 GUI framework: model structs, persistent widgets, "
                                              "reactive fields. This page is the same C++ as the desktop "
                                              "build, compiled to WebAssembly.",
                                   .buttons = {"Close"}}};
    core::Field<ui::Button> open_tool{{.text = "", .icon = gallery::icon("folder.svg"), .primary = false}};
    core::Field<ui::Button> save_tool{{.text = "", .icon = gallery::icon("save.svg"), .primary = false}};
    core::Field<ui::Button> wrap_tool{{.text = "Wrap", .primary = false, .checked = true}};
    core::Field<ui::Label<>> status{{"Controls"}};
    core::Field<ui::TabBar<>> tabs;
    gallery::Prefs prefs;
    gallery::ControlsTab controls{prefs.theme};
    gallery::CatalogTab catalog;
    gallery::TextTab text;
    gallery::DataTab data;
    gallery::PlotTab plot;
    gallery::PopupsTab popups;
    gallery::DockingTab docking{prefs.layout};
    gallery::BuildYourOwnTab build_your_own;
    core::Wiring wire;   // captures `this`: the gallery never moves

    Gallery() {
        wire.on(file_menu, [this](const ui::Menu& m) {
            if (m.chosen) popups.choose(*m.chosen == 0 ? app::FileDialogKind::Open : app::FileDialogKind::Save);
        });
        wire.on(open_tool, [this](const ui::Button&) { popups.choose(app::FileDialogKind::Open); });
        wire.on(save_tool, [this](const ui::Button&) { popups.choose(app::FileDialogKind::Save); });
        wire.on(tabs, [this](const ui::TabBar<>& t) {
            status.set({tab_names[std::min(t.selected, tab_names.size() - 1)]});
        });
        wire.on(view_menu, [this](const ui::Menu& m) {
            if (m.chosen) tabs.set({.selected = *m.chosen});
        });
        wire.on(help_menu, [this](const ui::Menu& m) {
            if (!m.chosen) return;
            auto dialog = about.get();
            dialog.open = true;
            dialog.chosen.reset();
            about.set(std::move(dialog));
        });
    }

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(file_menu);
            vb.widget(view_menu);
            vb.widget(help_menu);
            vb.spacer();
        });
        vb.toolbar([&] {
            vb.widget(open_tool).tooltip("Open…");
            vb.widget(save_tool).tooltip("Save as…");
            vb.widget(wrap_tool).tooltip("A toggle: stays pressed");
            vb.spacer();
        });
        vb.tabs(tabs, [&] {
            vb.tab(tab_names[0], [&](app::ViewBuilder& t) { t.scroll([&] { t.block(controls); }); });
            vb.tab(tab_names[1], [&](app::ViewBuilder& t) { t.block(catalog); });
            vb.tab(tab_names[2], [&](app::ViewBuilder& t) { t.scroll([&] { t.block(text); }); });
            vb.tab(tab_names[3], [&](app::ViewBuilder& t) { t.block(data); });
            vb.tab(tab_names[4], [&](app::ViewBuilder& t) { t.block(plot); });
            vb.tab(tab_names[5], [&](app::ViewBuilder& t) { t.block(popups); });
            vb.tab(tab_names[6], [&](app::ViewBuilder& t) { t.block(docking); });
            vb.tab(tab_names[7], [&](app::ViewBuilder& t) { t.scroll([&] { t.block(build_your_own); }); });
        });
        vb.status_bar([&] { vb.widget(status); vb.spacer(); });
        vb.dialog(about);
    }
};

int main(int argc, char* argv[]) {
    Gallery app;
    if (argc >= 2) {   // README capture: widget_gallery out.svg [tab number]
        if (argc >= 3) app.tabs.set({.selected = std::stoul(argv[2])});
        return showcase(argc, argv, app, 900, 640);
    }
    // .settings: the preferences come back at the next start, window size included.
    core::Connection theme_link;   // follows the Theme dropdown; ends with run()
    app::run(app, {.title = "PRISM Widget Gallery", .width = 1000, .height = 700,
                   .settings = &app.prefs, .organization = "prism", .name = "widget-gallery"},
             [&app, &theme_link](app::AppContext& ctx) {
        app.popups.ctx = &ctx;
        ctx.on_file_drop([&app](const input::FileDrop& drop) { app.popups.dropped(drop); });
        auto apply = [&ctx](gallery::Theme t) {
            if (t == gallery::Theme::System) ctx.follow_system_theme();
            else ctx.set_theme(t == gallery::Theme::Light ? ui::light_theme() : ui::dark_theme());
        };
        apply(value(app.prefs.theme));
        theme_link = app.prefs.theme.on_change().connect(apply);
    });
    theme_link.disconnect();
    app.popups.ctx = nullptr;   // the context ended with model_app
}
