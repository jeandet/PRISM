#pragma once

#include <prism/ui/text_segmentation.hpp>
#include <prism/core/utf8.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::ui {

// Editable text with a caret and a selection, as plain data. The selection is the span between
// `anchor` (where it started) and `cursor` (where the caret is); they are equal when nothing is
// selected. Both are byte offsets on character boundaries. Widgets map keys and clicks onto the
// pure functions below, so editing behaviour is tested without any widget.
struct TextEdit {
    std::string text;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    bool operator==(const TextEdit&) const = default;
};

namespace text_edit {

inline bool has_selection(const TextEdit& e) { return e.cursor != e.anchor; }

inline std::pair<std::size_t, std::size_t> selection(const TextEdit& e) {
    return std::minmax(e.cursor, e.anchor);
}

inline std::string_view selected(const TextEdit& e) {
    auto [lo, hi] = selection(e);
    return std::string_view(e.text).substr(lo, hi - lo);
}

// Moves the caret; `extend` keeps the anchor (shift held), otherwise the selection collapses.
inline TextEdit move_to(TextEdit e, std::size_t pos, bool extend) {
    e.cursor = core::utf8::snap(e.text, pos);
    if (!extend) e.anchor = e.cursor;
    return e;
}

inline TextEdit select_all(TextEdit e) {
    e.anchor = 0;
    e.cursor = e.text.size();
    return e;
}

inline TextEdit replace_selection(TextEdit e, std::string_view with) {
    auto [lo, hi] = selection(e);
    e.text.replace(lo, hi - lo, with);
    e.cursor = e.anchor = lo + with.size();
    return e;
}

// Types `s` over the selection. max_length (characters, 0 = no limit) applies to the result;
// `s` is cut on a character boundary to fit.
inline TextEdit insert(TextEdit e, std::string_view s, std::size_t max_length) {
    if (max_length > 0) {
        std::size_t kept = core::utf8::count(e.text) - core::utf8::count(selected(e));
        std::size_t room = kept < max_length ? max_length - kept : 0;
        if (room == 0 && !s.empty()) return e;   // no room: keep the selection, insert nothing
        s = s.substr(0, core::utf8::offset(s, room));
    }
    return replace_selection(std::move(e), s);
}

// Backspace: the selection if any, else the character before the caret.
inline TextEdit erase_back(TextEdit e) {
    if (!has_selection(e)) e.anchor = prev_grapheme(e.text, e.cursor);
    return replace_selection(std::move(e), "");
}

// Delete: the selection if any, else the character after the caret.
inline TextEdit erase_forward(TextEdit e) {
    if (!has_selection(e)) e.anchor = next_grapheme(e.text, e.cursor);
    return replace_selection(std::move(e), "");
}

} // namespace text_edit

// Undo/redo for one edited text. Each step stores the whole TextEdit before the change, so undo
// also restores the caret and the selection that was replaced. Consecutive typing merges into
// one step until the caret moves (break_group) or another kind of edit happens.
// simplify: whole-text snapshots, fine for fields and notes; a large document wants diffs.
class EditHistory {
public:
    enum class Kind { Typing, Other };
    static constexpr std::size_t capacity = 100;

    void record(const TextEdit& before, const TextEdit& after, Kind kind) {
        if (before.text == after.text) { break_group(); return; }   // caret/selection only
        redo_.clear();
        bool merge = kind == Kind::Typing && typing_ && !undo_.empty();
        if (!merge) push(undo_, before);
        typing_ = kind == Kind::Typing;
        last_text_ = after.text;
    }

    void break_group() { typing_ = false; }

    std::optional<TextEdit> undo(const TextEdit& current) { return step(undo_, redo_, current); }
    std::optional<TextEdit> redo(const TextEdit& current) { return step(redo_, undo_, current); }

    // False when the text was changed by someone else since this history last touched it: the
    // stored steps no longer apply.
    bool tracks(std::string_view text) const { return !last_text_ || *last_text_ == text; }
    void clear() { *this = {}; }

private:
    static void push(std::vector<TextEdit>& v, TextEdit e) {
        v.push_back(std::move(e));
        if (v.size() > capacity) v.erase(v.begin());
    }

    std::optional<TextEdit> step(std::vector<TextEdit>& from, std::vector<TextEdit>& to, const TextEdit& current) {
        if (from.empty()) return std::nullopt;
        TextEdit target = std::move(from.back());
        from.pop_back();
        push(to, current);
        typing_ = false;
        last_text_ = target.text;
        return target;
    }

    std::vector<TextEdit> undo_, redo_;
    bool typing_ = false;
    std::optional<std::string> last_text_;
};

} // namespace prism::ui
