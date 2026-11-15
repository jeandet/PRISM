#pragma once

#include <prism/ui/delegate.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace prism::ui {

namespace dialog_detail {

// What an open dialog's content was built from.
struct Shown {
    std::string title, message;
    std::vector<std::string> buttons;
    bool operator==(const Shown&) const = default;
};

// The dialog closes, reporting `choice` (none: dismissed with Escape).
// Defined in src/delegates/dialog_delegates.cpp.
void close(Field<Dialog>& field, WidgetNode& node, std::optional<std::size_t> choice);

} // namespace dialog_detail

// --- Dialog: a popup whose content the view builder makes from the dialog (a title, the message
// in a read-only text area, the app's widgets, a row of buttons; see ViewBuilder::dialog). The
// widget only says whether it is open and turns Escape into "dismissed". ---

inline void Widget<Dialog>::handle_input(Field<Dialog>& field, const InputEvent& ev, WidgetNode& node) {
    auto* kp = std::get_if<KeyPress>(&ev);
    if (kp && kp->key == keys::escape && field.get().open) dialog_detail::close(field, node, std::nullopt);
}

} // namespace prism::ui
