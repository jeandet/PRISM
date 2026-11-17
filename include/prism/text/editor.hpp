#pragma once

#include <prism/core/connection.hpp>
#include <prism/input/input_event.hpp>
#include <prism/text/document.hpp>
#include <prism/text/view.hpp>
#include <prism/ui/clipboard.hpp>

#include <string>
#include <string_view>
#include <vector>

// A document edited: a caret and a selection, moved and changed by keys, typed text, pastes and
// the mouse (doc/design/text-stack.md). The view draws them; this keeps them and edits the
// document. What `vb.text_editor(doc)` shows.
//
// Keys: arrows (Ctrl: by word), Home/End (the visual line; Ctrl: the document), Page Up/Down,
// Shift to select, Backspace/Delete (Ctrl: a word), Enter, Ctrl+A/C/X/Z/Y, Ctrl+Shift+Z; Tab and
// Shift+Tab indent when tab_inserts. Mouse: click, Shift+click, drag, double click (a word), triple
// click (a line).
// simplify: a bracket inside a string or a comment still counts; add when a language's rules
// track them.
namespace prism::text {

struct EditorOptions {
    ViewOptions view{};
    bool read_only = false;       // selectable and copyable, not changed
    bool tab_inserts = false;     // Tab types an indent (code) instead of moving the focus on
    bool auto_indent = false;     // Enter keeps the line's leading whitespace (code)
    bool match_brackets = false;  // the bracket at the caret and its match highlighted (code)
    // Rich text: Ctrl+B, Ctrl+I and Ctrl+E make the selection (or the word at the caret) strong,
    // emphasis or code, and again take it off; "# ", "- ", "1. " or "> " typed at a paragraph's
    // start make it a heading, a list item or a quote; Enter goes on with a list, and leaves it
    // on an empty item; Backspace at a block's start makes it a paragraph again.
    bool rich = false;
    std::string indent = "    ";
};

class Editor {
public:
    explicit Editor(Document& doc, EditorOptions options = {});
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    View view;

    // One input event; true if the editor changed what it shows (redraw).
    bool handle(const input::InputEvent& ev, const ui::Clipboard* clipboard);

    [[nodiscard]] Position caret() const { return caret_; }
    [[nodiscard]] Position anchor() const { return anchor_; }
    [[nodiscard]] Range selection() const { return {std::min(caret_, anchor_), std::max(caret_, anchor_)}; }
    [[nodiscard]] std::string selected_text() const { return doc_.text(selection()); }
    [[nodiscard]] const EditorOptions& options() const { return options_; }
    void set_caret(Position p, bool extend = false);
    void select_all();

private:
    Document& doc_;
    EditorOptions options_;
    Position caret_{}, anchor_{};
    float column_x_ = -1.f;    // where up and down aim; -1: the caret's own x
    bool dragging_ = false;
    bool dragging_map_ = false;    // a press in the minimap: moving scrolls, not selects
    std::string preedit_;          // an input method's composition, shown at the caret
    std::size_t preedit_caret_ = 0;
    core::Connection follow_;

    bool key(const input::KeyPress& k, const ui::Clipboard* clipboard);
    bool mouse_button(const input::MouseButton& b);
    void type(std::string_view text);
    void erase(Range r);
    void enter();
    void indent(bool more);
    bool copy(const ui::Clipboard* clipboard) const;

    Position clamp(Position p) const;
    Position next_char(Position p) const;
    Position prev_char(Position p) const;
    Position next_word(Position p) const;
    Position prev_word(Position p) const;
    Range word_at(Position p) const;
    std::vector<Range> bracket_pair() const;
    void toggle(Style s);
    void markdown_shortcut();
    bool leave_block();          // Backspace at the start of a heading, quote or list item
    void continue_block(bool at_start);   // after Enter: what the new block becomes
    void move(Position to, bool extend);   // a caret jump: the next typing starts a new undo step
    void show();                           // the view's marks follow the caret, and it scrolls to it
    View::Marks marks() const;
};

} // namespace prism::text
