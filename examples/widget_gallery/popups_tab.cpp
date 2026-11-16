#include "popups_tab.hpp"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace gallery {

PopupsTab::PopupsTab() {
    wire.on(ask, [this](const auto&) { open(confirm); });
    wire.on(rename, [this](const auto&) { open(rename_dialog); });
    wire.on(options, [this](const auto&) { options_popup.set({.open = true, .modal = false}); });
    wire.on(open_file, [this](const auto&) { choose(prism::app::FileDialogKind::Open); });
    wire.on(save_file, [this](const auto&) { choose(prism::app::FileDialogKind::Save); });
    wire.on(message, [this](const auto&) {
        if (ctx) ctx->show_message(prism::app::MessageKind::Info, "PRISM", "A message box from the platform.");
        say("Message box asked for");
    });
    wire.on(confirm, [this](const prism::ui::Dialog& d) {
        if (!d.open) say(d.chosen == 0 ? "Deleted (pretend)" : "Delete cancelled");
    });
    wire.on(rename_dialog, [this](const prism::ui::Dialog& d) {
        if (!d.open) say(d.chosen == 0 ? fmt::format("Renamed to {}{}", new_name.get().value,
                                                     backup.get().checked ? " (backup kept)" : "")
                                       : "Rename cancelled");
    });
    wire.on(edit_menu, [this](const prism::ui::ContextMenu& m) {
        if (m.chosen) say("Context menu: " + m.items[*m.chosen].text);
    });
}

void PopupsTab::choose(prism::app::FileDialogKind kind) {
    if (!ctx) return;
    ctx->choose_files({.kind = kind, .title = kind == prism::app::FileDialogKind::Save ? "Save as" : "Open",
                       .filters = {{"Text files", "txt;md"}, {"All files", "*"}}, .start = {}},
                      [this](prism::app::FileDialogResult r) {
                          say(!r.error.empty() ? "File dialog: " + r.error
                              : r.paths.empty() ? std::string("File dialog cancelled")
                                                : "Chose " + r.paths.front());
                      });
}

void PopupsTab::dropped(const prism::input::FileDrop& drop) {
    say(fmt::format("Dropped {} file(s): {}", drop.paths.size(), drop.paths.front()));
}

void PopupsTab::view(prism::app::WidgetTree::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(hint);
    vb.gap(8.f);
    vb.hstack([&] {
        vb.widget(ask).tooltip("A modal dialog: Enter picks the first button, Escape cancels");
        vb.gap(8.f);
        vb.widget(rename).tooltip("A dialog holding a form");
        vb.gap(8.f);
        vb.widget(options).tooltip("A popover: a click outside closes it");
        // Declared here, it opens here: a non-modal popup is placed at its declaration.
        vb.popup(options_popup, [&](prism::app::WidgetTree::ViewBuilder& p) { p.vstack(grid, snap); });
        vb.spacer();
    });
    vb.gap(8.f);
    vb.hstack([&] {
        vb.widget(open_file).tooltip("The platform's file dialog (in a browser: not available)");
        vb.gap(8.f);
        vb.widget(save_file);
        vb.gap(8.f);
        vb.widget(message).tooltip("The platform's message box");
        vb.spacer();
    });
    vb.gap(8.f);
    vb.widget(result);
    vb.spacer();   // the page fills the tab, so a right-click anywhere on it is inside
    vb.context_menu(edit_menu);
    vb.dialog(confirm);
    vb.dialog(rename_dialog, [&](prism::app::WidgetTree::ViewBuilder& d) {
        d.widget(new_name).focus_on_open();
        d.widget(file_type).tooltip("A dropdown inside a dialog: its list opens over the dialog");
        d.widget(backup);
    });
}

void PopupsTab::say(std::string text) { result.set({std::move(text)}); }

void PopupsTab::open(prism::core::Field<prism::ui::Dialog>& d) {
    auto v = d.get();
    v.open = true;
    v.chosen.reset();
    d.set(v);
}

} // namespace gallery
