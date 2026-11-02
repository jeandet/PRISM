// Popups: dialogs (one holding a form), a popover, a context menu, the platform's file dialogs and
// message boxes, and drag and drop. The result of each shows in the line at the bottom.
#pragma once

#include <prism/prism.hpp>

#include <fmt/format.h>

#include <string>
#include <vector>

namespace gallery {

enum class FileType { Text, Markdown, Csv };

struct PopupsTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Popups", .heading = true}};
    prism::core::Field<prism::ui::Label<>> hint{{"Right-click anywhere on this page for a context menu. "
                                                 "Drop a file on the window to see its name below."}};
    prism::core::Field<prism::ui::Button> ask{{"Confirm…"}};
    prism::core::Field<prism::ui::Button> rename{{"Rename…"}};
    prism::core::Field<prism::ui::Button> options{{.text = "Options", .primary = false}};
    prism::core::Field<prism::ui::Button> open_file{{.text = "Open file…", .primary = false}};
    prism::core::Field<prism::ui::Button> save_file{{.text = "Save as…", .primary = false}};
    prism::core::Field<prism::ui::Button> message{{.text = "Message box", .primary = false}};
    prism::core::Field<prism::ui::Label<>> result{{"Nothing yet"}};

    prism::core::Field<prism::ui::Dialog> confirm{{.title = "Delete 3 files?",
                                                   .message = "They go to the trash. You can restore them from there.",
                                                   .buttons = {"Delete", "Cancel"}}};
    prism::core::Field<prism::ui::Dialog> rename_dialog{{.title = "Rename", .message = "",
                                                         .buttons = {"Rename", "Cancel"}}};
    prism::core::Field<prism::ui::TextField<>> new_name{{.value = "report.txt"}};
    prism::core::Field<prism::ui::Checkbox> backup{{.checked = true, .label = "Keep a backup"}};
    prism::core::Field<FileType> file_type{FileType::Text};   // a dropdown inside the dialog
    prism::core::Field<prism::ui::Popup> options_popup{{.open = false, .modal = false}};
    prism::core::Field<prism::ui::Checkbox> grid{{.checked = true, .label = "Show grid"}};
    prism::core::Field<prism::ui::Checkbox> snap{{.checked = false, .label = "Snap to grid"}};
    prism::core::Field<prism::ui::ContextMenu> edit_menu{{.items = {"Cut", "Copy", "Paste"}}};

    prism::app::AppContext* ctx = nullptr;   // set while the app runs: file dialogs, message boxes
    std::vector<prism::core::Connection> wiring;   // captures `this`: the tab never moves

    PopupsTab() {
        wiring.push_back(ask.on_change().connect([this](const auto&) { open(confirm); }));
        wiring.push_back(rename.on_change().connect([this](const auto&) { open(rename_dialog); }));
        wiring.push_back(options.on_change().connect([this](const auto&) { options_popup.set({.open = true, .modal = false}); }));
        wiring.push_back(open_file.on_change().connect([this](const auto&) { choose(prism::app::FileDialogKind::Open); }));
        wiring.push_back(save_file.on_change().connect([this](const auto&) { choose(prism::app::FileDialogKind::Save); }));
        wiring.push_back(message.on_change().connect([this](const auto&) {
            if (ctx) ctx->show_message(prism::app::MessageKind::Info, "PRISM", "A message box from the platform.");
            say("Message box asked for");
        }));
        wiring.push_back(confirm.on_change().connect([this](const prism::ui::Dialog& d) {
            if (!d.open) say(d.chosen == 0 ? "Deleted (pretend)" : "Delete cancelled");
        }));
        wiring.push_back(rename_dialog.on_change().connect([this](const prism::ui::Dialog& d) {
            if (!d.open) say(d.chosen == 0 ? fmt::format("Renamed to {}{}", new_name.get().value,
                                                         backup.get().checked ? " (backup kept)" : "")
                                           : "Rename cancelled");
        }));
        wiring.push_back(edit_menu.on_change().connect([this](const prism::ui::ContextMenu& m) {
            if (m.chosen) say("Context menu: " + m.items[*m.chosen]);
        }));
    }

    void choose(prism::app::FileDialogKind kind) {
        if (!ctx) return;
        ctx->choose_files({.kind = kind, .title = kind == prism::app::FileDialogKind::Save ? "Save as" : "Open",
                           .filters = {{"Text files", "txt;md"}, {"All files", "*"}}, .start = {}},
                          [this](prism::app::FileDialogResult r) {
                              say(!r.error.empty() ? "File dialog: " + r.error
                                  : r.paths.empty() ? std::string("File dialog cancelled")
                                                    : "Chose " + r.paths.front());
                          });
    }

    void dropped(const prism::input::FileDrop& drop) {
        say(fmt::format("Dropped {} file(s): {}", drop.paths.size(), drop.paths.front()));
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
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

private:
    void say(std::string text) { result.set({std::move(text)}); }

    static void open(prism::core::Field<prism::ui::Dialog>& d) {
        auto v = d.get();
        v.open = true;
        v.chosen.reset();
        d.set(v);
    }
};

} // namespace gallery
