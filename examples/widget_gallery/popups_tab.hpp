// Popups: dialogs (one holding a form), a popover, a context menu, the platform's file dialogs and
// message boxes, and drag and drop. The result of each shows in the line at the bottom.
#pragma once

#include <prism/prism.hpp>

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
    prism::core::Field<prism::ui::ContextMenu> edit_menu{{.items = {{"Cut"}, {"Copy"}, {"Paste"}}}};

    prism::app::AppContext* ctx = nullptr;   // set while the app runs: file dialogs, message boxes
    prism::core::Wiring wire;   // captures `this`: the tab never moves

    PopupsTab();
    void choose(prism::app::FileDialogKind kind);
    void dropped(const prism::input::FileDrop& drop);
    void view(prism::app::ViewBuilder& vb);   // popups_tab.cpp

private:
    void say(std::string text);
};

} // namespace gallery
