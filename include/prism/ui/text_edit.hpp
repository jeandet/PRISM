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

// One edit as what it changed: at byte `at`, `removed` became `inserted`, with the caret and
// selection before and after. Undo and redo replay it on the text as it is.
struct TextChange {
    std::size_t at = 0;
    std::string removed, inserted;
    std::size_t cursor_before = 0, anchor_before = 0, cursor_after = 0, anchor_after = 0;
};

namespace text_edit {

// How many bytes `a` and `b` share at their start (at their end: common_suffix), at most `limit`.
// Whole blocks are compared first, with memcmp, then single bytes.
inline std::size_t common_prefix(std::string_view a, std::string_view b, std::size_t limit) {
    constexpr std::size_t block = 256;
    std::size_t n = 0;
    while (n + block <= limit && a.substr(n, block) == b.substr(n, block)) n += block;
    while (n < limit && a[n] == b[n]) ++n;
    return n;
}

inline std::size_t common_suffix(std::string_view a, std::string_view b, std::size_t limit) {
    constexpr std::size_t block = 256;
    std::size_t n = 0;
    while (n + block <= limit && a.substr(a.size() - n - block, block) == b.substr(b.size() - n - block, block)) n += block;
    while (n < limit && a[a.size() - 1 - n] == b[b.size() - 1 - n]) ++n;
    return n;
}

// The bytes that differ between `before` and `after`: their common start and end are left out.
// The start is taken no later than the caret or selection, where an edit begins: in "aaa" a
// typed "a" is the one at the caret, so the next one typed joins it.
inline TextChange change_between(const TextEdit& before, const TextEdit& after) {
    std::string_view a = before.text, b = after.text;
    auto edit_start = std::min({selection(before).first, selection(after).first, a.size(), b.size()});
    auto prefix = common_prefix(a, b, edit_start);
    auto suffix = common_suffix(a, b, std::min(a.size(), b.size()) - prefix);
    return {.at = prefix,
            .removed = std::string(a.substr(prefix, a.size() - prefix - suffix)),
            .inserted = std::string(b.substr(prefix, b.size() - prefix - suffix)),
            .cursor_before = before.cursor, .anchor_before = before.anchor,
            .cursor_after = after.cursor, .anchor_after = after.anchor};
}

inline TextEdit undo_change(TextEdit e, const TextChange& c) {
    e.text.replace(c.at, c.inserted.size(), c.removed);
    e.cursor = c.cursor_before;
    e.anchor = c.anchor_before;
    return e;
}

inline TextEdit redo_change(TextEdit e, const TextChange& c) {
    e.text.replace(c.at, c.removed.size(), c.inserted);
    e.cursor = c.cursor_after;
    e.anchor = c.anchor_after;
    return e;
}

} // namespace text_edit

// Undo/redo for one edited text. Each step keeps only what the edit changed (TextChange), and
// the caret and selection around it, so undo also restores the selection that was replaced.
// Consecutive typing merges into one step until the caret moves (break_group) or another kind
// of edit happens.
class EditHistory {
public:
    enum class Kind { Typing, Other };
    static constexpr std::size_t capacity = 100;

    void record(const TextEdit& before, const TextEdit& after, Kind kind) {
        if (before.text == after.text) { break_group(); return; }   // caret/selection only
        redo_.clear();
        auto change = text_edit::change_between(before, after);
        if (!(kind == Kind::Typing && typing_ && merge_typing(change))) push(undo_, std::move(change));
        typing_ = kind == Kind::Typing;
        last_text_ = after.text;
    }

    void break_group() { typing_ = false; }

    std::optional<TextEdit> undo(const TextEdit& current) {
        return step(undo_, redo_, [&](const TextChange& c) { return text_edit::undo_change(current, c); });
    }
    std::optional<TextEdit> redo(const TextEdit& current) {
        return step(redo_, undo_, [&](const TextChange& c) { return text_edit::redo_change(current, c); });
    }

    // False when the text was changed by someone else since this history last touched it: the
    // stored steps no longer apply.
    bool tracks(std::string_view text) const { return !last_text_ || *last_text_ == text; }
    void clear() { *this = {}; }

    // The text the steps keep, in bytes (both directions).
    std::size_t stored_bytes() const {
        std::size_t n = 0;
        for (auto* v : {&undo_, &redo_})
            for (auto& c : *v) n += c.removed.size() + c.inserted.size();
        return n;
    }

private:
    static void push(std::vector<TextChange>& v, TextChange c) {
        v.push_back(std::move(c));
        if (v.size() > capacity) v.erase(v.begin());
    }

    // Typing right after the last typed text joins its step.
    bool merge_typing(const TextChange& c) {
        if (undo_.empty()) return false;
        auto& last = undo_.back();
        if (!c.removed.empty() || c.at != last.at + last.inserted.size()) return false;
        last.inserted += c.inserted;
        last.cursor_after = c.cursor_after;
        last.anchor_after = c.anchor_after;
        return true;
    }

    std::optional<TextEdit> step(std::vector<TextChange>& from, std::vector<TextChange>& to, auto apply) {
        if (from.empty()) return std::nullopt;
        TextEdit target = apply(from.back());
        push(to, std::move(from.back()));
        from.pop_back();
        typing_ = false;
        last_text_ = target.text;
        return target;
    }

    std::vector<TextChange> undo_, redo_;
    bool typing_ = false;
    std::optional<std::string> last_text_;
};

} // namespace prism::ui
