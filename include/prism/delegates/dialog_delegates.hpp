#pragma once

#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <string_view>
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
inline void close(Field<Dialog>& field, WidgetNode& node, std::optional<std::size_t> choice) {
    node.popup_area.reset();
    node.dirty = true;
    auto d = field.get();
    d.open = false;
    d.chosen = choice;
    ++d.activations;
    field.set(d);
}

} // namespace dialog_detail

// --- Dialog: a popup whose content the view builder makes from the dialog (a title, the message
// in a read-only text area, the app's widgets, a row of buttons; see ViewBuilder::dialog). The
// widget only says whether it is open and turns Escape into "dismissed". ---

inline void Widget<Dialog>::record(DrawList&, const Field<Dialog>& field, WidgetNode& node) {
    const Dialog& d = field.get();
    node.overlay_draws.clear();
    node.popup_content_open = d.open;
    node.popup_modal = true;
    if (!d.open) {
        node.popup_area.reset();
        node.edit_state.reset();
        return;
    }
    // The content was built from the dialog as it was: rebuild it when what it shows changed
    // while open (a new message, other buttons), not on every re-record.
    auto& shown = node.get_or_create<dialog_detail::Shown>();
    dialog_detail::Shown now{d.title, d.message, d.buttons};
    if (shown != now) {
        node.popup_rebuild = true;
        shown = std::move(now);
    }
}

inline void Widget<Dialog>::handle_input(Field<Dialog>& field, const InputEvent& ev, WidgetNode& node) {
    auto* kp = std::get_if<KeyPress>(&ev);
    if (kp && kp->key == keys::escape && field.get().open) dialog_detail::close(field, node, std::nullopt);
}

// --- Popup: its content is ordinary widgets the tree builds, lays out and draws; the widget only
// says whether it should be open and closes itself on Escape. ---

inline void Widget<Popup>::record(DrawList&, const Field<Popup>& field, WidgetNode& node) {
    node.overlay_draws.clear();
    node.popup_content_open = field.get().open;
    node.popup_modal = field.get().modal;
    if (!field.get().open) node.popup_area.reset();
}

inline void Widget<Popup>::handle_input(Field<Popup>& field, const InputEvent& ev, WidgetNode& node) {
    auto* kp = std::get_if<KeyPress>(&ev);
    if (!kp || kp->key != keys::escape || !field.get().open) return;
    auto p = field.get();
    p.open = false;
    field.set(p);
    node.popup_area.reset();
    node.dirty = true;
}

} // namespace prism::ui
