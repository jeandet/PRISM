#pragma once

#include <prism/core/utf8.hpp>
#include <prism/ui/clipboard.hpp>
#include <prism/ui/text_edit.hpp>
#include <prism/ui/text_metrics.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cassert>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace prism::ui {

namespace text_detail {

std::string mask_string(size_t len);

constexpr Height tf_widget_h{30.f};
constexpr float tf_font_size = 14.f;

// The byte of `shown` matching byte `at` of `value`: they have the same characters, not always
// the same bytes (the Password mask draws a dot per character).
inline size_t shown_offset(std::string_view value, std::string_view shown, size_t at) {
    return core::utf8::offset(shown, core::utf8::count_upto(value, core::utf8::snap(value, at)));
}

// Caret x for a byte offset, measured on the text as displayed (the Password mask draws dots).
template <typename DisplayFn>
float caret_x(const TextMeasurer* m, std::string_view value, size_t cursor, DisplayFn display_fn) {
    std::string shown = display_fn(std::string(value));
    return caret_x_at(m, shown, shown_offset(value, shown, cursor), tf_font_size);
}

// Where the text starts: a field whose text starts right to left sits on the right while it fits
// (as a text area's right-to-left paragraph does); a longer one scrolls as before.
template <typename DisplayFn>
float text_x0(const TextMeasurer* m, std::string_view value, Width area_w, DisplayFn display_fn) {
    std::string shown = display_fn(std::string(value));
    if (paragraph_direction(shown) != TextDirection::Rtl) return 0.f;
    return std::max(0.f, area_w.raw() - text_width(m, shown, tf_font_size));
}

// The x spans showing [lo, hi) of `value` as displayed: several when it crosses a direction change.
template <typename DisplayFn>
auto range_spans(const TextMeasurer* m, std::string_view value, size_t lo, size_t hi, DisplayFn display_fn) {
    std::string shown = display_fn(std::string(value));
    return selection_spans(m, shown, shown_offset(value, shown, lo), shown_offset(value, shown, hi), tf_font_size);
}

// Byte offset in `value` nearest to x, hit-tested on the displayed text. Display and value have
// the same number of characters, so the boundary maps across by character count.
template <typename DisplayFn>
size_t cursor_at(const TextMeasurer* m, std::string_view value, float x, DisplayFn display_fn) {
    std::string shown = display_fn(std::string(value));
    assert(core::utf8::count(shown) == core::utf8::count(value) && "display_fn must keep length");
    size_t byte = index_at(m, shown, tf_font_size, x);
    return core::utf8::offset(value, core::utf8::count_upto(shown, byte));
}
// tf_padding is used in both a horizontal (inset from left edge) and vertical
// (inset from top edge) role below — split rather than force one tag.
constexpr Width tf_padding_x{4.f};
constexpr Height tf_padding_y{4.f};
constexpr Width tf_cursor_w{2.f};

// The text as displayed while composing: the preedit inserted at the caret.
std::string with_preedit(std::string_view value, std::size_t cursor, std::string_view preedit);

inline std::string plain_display(const std::string& v) { return v; }
inline std::string masked_display(const std::string& v) { return mask_string(core::utf8::count(v)); }

// What drawing a single-line field needs of its value: one body serves TextField, Password and
// ComboBox. `display` is plain_display, or masked_display for a secret field.
struct TextFieldDraw {
    std::string_view value;
    std::string_view placeholder;
    std::string (*display)(const std::string&);
};

void draw_text_field(DrawList& dl, const TextFieldDraw& field, const WidgetNode& node);

template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node,
                       DisplayFn display_fn) {
    draw_text_field(dl, TextFieldDraw{field.get().value, field.get().placeholder, display_fn}, node);
}

// Keys shared by TextField and TextArea, as edits on a TextEdit. Returns nullopt for keys the
// widget handles itself (home/end, up/down, enter).
std::optional<TextEdit> common_edit_key(const TextEdit& e, const KeyPress& kp);

// Left/Right on a line holding right-to-left text move across the screen (visual_step), not
// through memory: the caret's new offset in `line`, or `at` itself at the line's edge on screen.
// nullopt for other keys, a plain left-to-right line, or a selection to collapse (that stays as
// common_edit_key does it: Left goes to the selection's start).
std::optional<std::size_t> visual_arrow(std::string_view line, std::size_t at, const TextEdit& e,
                                        const KeyPress& kp, const TextMeasurer* m, float size,
                                        FontRole role, TextDirection dir);

// Ctrl/Cmd+C and +X. Returns nullopt for other keys. Without a clipboard, a selection, or
// permission (secret fields), the key is consumed and nothing happens -- in particular cut never
// deletes text it could not put on the clipboard.
std::optional<TextEdit> clipboard_key(const TextEdit& e, const KeyPress& kp,
                                      const Clipboard* cb, bool allow_copy);

// Pasted text for a single-line field: line breaks become spaces.
std::string single_line(std::string_view s);

// Pasted text for a multi-line area: Windows/old-Mac line ends become '\n'.
std::string normalized_lines(std::string_view s);

// Ctrl/Cmd+Z undoes; Ctrl/Cmd+Shift+Z and Ctrl/Cmd+Y redo. nullopt for other keys.
std::optional<std::optional<TextEdit>> history_key(EditHistory& h, const TextEdit& e, const KeyPress& kp);

// Stores an IME composition in the edit state. The IME's caret offset is ambiguous across
// platforms (characters on X11, bytes on Wayland): used only when it is a valid byte boundary,
// else the caret goes after the preedit.
template <typename EditState>
void set_composition(EditState& es, WidgetNode& node, const Composition& c) {
    es.preedit = core::utf8::sanitize(c.text);
    auto start = static_cast<std::size_t>(c.start);
    bool usable = c.start >= 0 && start <= es.preedit.size() && core::utf8::snap(es.preedit, start) == start;
    es.preedit_caret = usable ? start : es.preedit.size();
    node.dirty = true;
}

// Writes an edit back: the field only if the text changed, the caret and selection always.
template <typename Sentinel, typename EditState>
void apply_edit(Field<Sentinel>& field, WidgetNode& node, EditState& es,
                const TextEdit& before, const TextEdit& after) {
    if (after.text != before.text) {
        auto sf = field.get();
        sf.value = std::remove_cvref_t<decltype(sf.value)>(after.text);
        field.set(sf);
    }
    std::optional<size_t> anchor;
    if (after.anchor != after.cursor) anchor = after.anchor;
    if (after.cursor != es.cursor || anchor != es.anchor) node.dirty = true;
    es.cursor = after.cursor;
    es.anchor = anchor;
}

template <typename Sentinel, typename EditState>
TextEdit current_edit(const Field<Sentinel>& field, EditState& es) {
    const auto& v = field.get().value;
    TextEdit e{std::string(v.data(), v.size()), 0, 0};
    if (!es.history.tracks(e.text)) es.history.clear();   // changed by the program: steps are stale
    e.cursor = core::utf8::snap(e.text, es.cursor);   // the value may have changed under us
    e.anchor = core::utf8::snap(e.text, es.anchor.value_or(es.cursor));
    return e;
}

// Mouse: press places the caret (shift extends) and starts a drag; moves extend while dragging.
template <typename EditState, typename HitFn>
std::optional<TextEdit> mouse_edit(const TextEdit& e, EditState& es, const InputEvent& ev, HitFn hit) {
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == buttons::left) {
        if (!mb->pressed) { es.dragging = false; return std::nullopt; }
        es.dragging = true;
        return text_edit::move_to(e, hit(mb->position), (mb->mods & mods::shift) != 0);
    }
    if (auto* mm = std::get_if<MouseMove>(&ev); mm && es.dragging) {
        // The release can be lost (scrollbar drag, window manager); the held buttons can't.
        if (!(mm->buttons & buttons::left_mask)) { es.dragging = false; return std::nullopt; }
        return text_edit::move_to(e, hit(mm->position), true);
    }
    return std::nullopt;
}

// `takes_composition`: false when the widget refuses an IME composition (a secret field, a
// read-only area); `allow_copy`: false for secret fields (Password), which must never reach the
// clipboard. The history key applies its own edit and ends the event, as it does in both widgets.
enum class SharedText { Done, Edit, NotShared };

struct SharedTextOutcome {
    SharedText kind = SharedText::NotShared;   // NotShared: the widget's own keys and mouse
    std::optional<TextEdit> edit;              // Edit: the caller's `after`
};

// The part of a text event TextField and TextArea handle the same way: composition, typed and
// pasted text, and the history and clipboard keys (pasteFn normalises the pasted text).
template <typename Sentinel, typename EditState, typename PasteFn>
SharedTextOutcome shared_text_event(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node,
                                    EditState& es, const TextEdit& before, PasteFn paste_fn,
                                    bool takes_composition, bool allow_copy) {
    if (auto* co = std::get_if<Composition>(&ev)) {
        if (takes_composition) set_composition(es, node, *co);
        return {SharedText::Done, std::nullopt};
    }
    if (std::holds_alternative<TextInput>(ev) || std::holds_alternative<Paste>(ev)) es.preedit.clear();

    std::optional<TextEdit> after;
    if (auto* ti = std::get_if<TextInput>(&ev)) {
        after = text_edit::insert(before, ti->text, field.get().max_length);
    } else if (auto* pa = std::get_if<Paste>(&ev)) {
        after = text_edit::insert(before, paste_fn(pa->text), field.get().max_length);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (auto h = history_key(es.history, before, *kp)) {
            if (*h) apply_edit(field, node, es, before, **h);
            return {SharedText::Done, std::nullopt};   // an undo step is not itself recorded
        }
        auto* cb = node.theme ? node.theme->clipboard : nullptr;
        if (auto c = clipboard_key(before, *kp, cb, allow_copy)) after = c;
        else return {SharedText::NotShared, std::nullopt};
    } else {
        return {SharedText::NotShared, std::nullopt};
    }
    return {SharedText::Edit, after};
}

// The tail both widgets end an edit with: one history step, then the edit itself.
template <typename Sentinel, typename EditState>
void record_and_apply(Field<Sentinel>& field, WidgetNode& node, EditState& es, const TextEdit& before,
                      const TextEdit& after, const InputEvent& ev) {
    es.history.record(before, after, std::holds_alternative<TextInput>(ev)
                                          ? EditHistory::Kind::Typing : EditHistory::Kind::Other);
    apply_edit(field, node, es, before, after);
}

// allow_copy: false for secret fields (Password), which must never reach the clipboard.
template <typename Sentinel, typename DisplayFn>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node,
                             DisplayFn display_fn, bool allow_copy) {
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    auto& es = node.get_or_create<TextEditState>();
    const TextEdit before = current_edit(field, es);

    // A secret field never shows plaintext, so it refuses a composition too.
    auto shared = shared_text_event(field, ev, node, es, before, single_line, allow_copy, allow_copy);
    if (shared.kind == SharedText::Done) return;
    std::optional<TextEdit> after = shared.edit;
    if (shared.kind == SharedText::NotShared) {
        if (auto* kp = std::get_if<KeyPress>(&ev)) {
            bool shift = (kp->mods & mods::shift) != 0;
            if (kp->key == keys::home) after = text_edit::move_to(before, 0, shift);
            else if (kp->key == keys::end) after = text_edit::move_to(before, before.text.size(), shift);
            // A secret field shows dots, which read left to right: its arrows stay logical.
            else if (auto to = allow_copy ? visual_arrow(before.text, before.cursor, before, *kp, m, tf_font_size,
                                                         FontRole::Ui, TextDirection::Auto)
                                          : std::nullopt)
                after = text_edit::move_to(before, *to, shift);
            else after = common_edit_key(before, *kp);
        } else {
            Width area_w = delegate_detail::widget_w(node) - tf_padding_x * 2.f;
            float x0 = text_x0(m, before.text, area_w, display_fn);
            after = mouse_edit(before, es, ev, [&](Point p) {
                return cursor_at(m, before.text, p.x.raw() - tf_padding_x.raw() + es.scroll_offset.raw() - x0,
                                 display_fn);
            });
        }
    }
    if (after) record_and_apply(field, node, es, before, *after, ev);

    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - tf_padding_x * 2.f;
    auto now = std::string_view(field.get().value.data(), field.get().value.size());
    Width cursor_px{text_x0(m, now, text_area_w, display_fn) + caret_x(m, now, es.cursor, display_fn)};
    if (cursor_px.raw() - es.scroll_offset.raw() > text_area_w.raw())
        es.scroll_offset = DX{cursor_px.raw() - text_area_w.raw()};
    if (cursor_px.raw() < es.scroll_offset.raw())
        es.scroll_offset = DX{cursor_px.raw()};
}

// --- TextArea helpers ---

using ui::LineSpan;

struct LineCol {
    size_t line;
    size_t col;
};

// TextArea text is fixed-width: columns stay aligned for code and tabular content.
constexpr FontRole ta_font_role = FontRole::Mono;   // the default; TextArea::font picks

using ui::is_wrap_space;
using ui::word_break;

inline std::vector<LineSpan> wrap_lines(std::string_view text, Width text_area_w,
                                        const TextMeasurer* m, float size) {
    return ui::wrap_lines(text, text_area_w, m, size, ta_font_role);
}

inline std::string_view line_text(std::string_view text, LineSpan line) {
    return text.substr(std::min(line.start, text.size()), line.length);
}

// How a wrapped line sits: its paragraph's direction (every line of a paragraph shares it, where
// alone a line would guess from its own first letter), and where it starts: a right-to-left
// paragraph's lines are right-aligned.
struct LinePlace {
    TextDirection dir = TextDirection::Ltr;
    float x0 = 0.f;
};

// The text of the paragraph line `span` starts, up to its newline.
inline std::string_view paragraph_from(std::string_view value, LineSpan span) {
    auto rest = value.substr(std::min(span.start, value.size()));
    return rest.substr(0, rest.find('\n'));
}

// The line as drawn: a direction mark first (U+200F right-to-left, U+200E left-to-right) when
// the renderer, which reads one line at a time and guesses its direction, would guess wrong.
std::string drawn_line(std::string_view text, TextDirection dir);

// `col` is a character column within the wrapped line; cursors are byte offsets.
// Whether line i ends where the next begins: a wrap inside a paragraph, not a newline.
inline bool soft_wrap_after(std::span<const LineSpan> lines, size_t i) {
    return i + 1 < lines.size() && lines[i + 1].start == lines[i].start + lines[i].length;
}

// Each wrapped line's place. A paragraph's direction is worked out once, at its first line.
std::vector<LinePlace> place_lines(std::string_view value, std::span<const LineSpan> lines, Width area_w,
                                   const TextMeasurer* m, float size, FontRole role);

// `upstream`: at a soft wrap, the caret is at the end of the line above (see
// TextAreaEditState::upstream), not at the start of the next.
LineCol cursor_to_line_col(std::string_view text, size_t cursor, std::span<const LineSpan> lines,
                           bool upstream = false);

size_t line_col_to_cursor(std::string_view text, size_t line, size_t col,
                          std::span<const LineSpan> lines);

// ta_padding is used in both a horizontal and vertical role below — split
// rather than force one tag, same reasoning as tf_padding_x/y above.
constexpr Width ta_padding_x{4.f};
constexpr Height ta_padding_y{4.f};
constexpr float ta_font_size = 14.f;
inline Height ta_line_h(const TextMeasurer* m, FontRole role = ta_font_role) {
    return Height{line_height(m, ta_font_size, role)};
}
constexpr Width ta_cursor_w{2.f};

// What the wheel and the scrollbar need (see app::get_scroll_view), and the offset kept inside
// the content when it shrinks.
void publish_scroll(WidgetNode& node, TextAreaEditState& es, Height content_h, Width text_area_w,
                    Height text_area_h);

// What drawing a text area needs of its value (the same cut as TextFieldDraw, on many lines).
struct TextAreaDraw {
    std::string_view value;
    std::string_view placeholder;
    std::size_t rows;
    std::span<const TextRange> highlights;
    bool read_only;
    FontRole font;
};

void draw_text_area(DrawList& dl, const TextAreaDraw& field, WidgetNode& node);

template <typename Sentinel>
void text_area_record(DrawList& dl, const Field<Sentinel>& field, WidgetNode& node) {
    draw_text_area(dl, TextAreaDraw{field.get().value, field.get().placeholder, field.get().rows,
                                    field.get().highlights, field.get().read_only, field.get().font},
                   node);
}

template <typename Sentinel>
void text_area_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = node.get_or_create<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    const FontRole role = field.get().font;
    const TextEdit before = current_edit(field, es);
    std::string_view value = before.text;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(field.get().rows) * ta_line_h(m, role).raw()};
    auto wrapped = ui::wrap_lines(value, text_area_w, m, ta_font_size, role);
    auto places = place_lines(value, wrapped, text_area_w, m, ta_font_size, role);
    auto [line, col] = cursor_to_line_col(value, before.cursor, wrapped, es.upstream);
    std::optional<TextEdit> after;
    bool upstream_after = false;   // only End and a landing past a line's end keep the line above
    // The caret at `col` on line `to`; past the end of a soft-wrapped line it stays on that line.
    auto at_line_col = [&](size_t to, size_t to_col) {
        upstream_after = soft_wrap_after(wrapped, to) && to_col >= core::utf8::count(line_text(value, wrapped[to]));
        return line_col_to_cursor(value, to, to_col, wrapped);
    };
    auto* key = std::get_if<KeyPress>(&ev);
    bool vertical = key && (key->key == keys::up || key->key == keys::down);
    bool hover = std::holds_alternative<MouseMove>(ev) && !es.dragging;
    if (!vertical && !hover && !std::holds_alternative<KeyRelease>(ev)) es.goal_x.reset();
    if (es.goal_cursor != before.cursor) es.goal_x.reset();   // the caret moved some other way
    // The caret on line `to`, as near to goal_x as that line allows.
    auto vertical_target = [&](size_t to) {
        if (!es.goal_x) {
            auto text = line_text(value, wrapped[line]);
            es.goal_x = places[line].x0
                      + caret_x_at(m, text, core::utf8::offset(text, col), ta_font_size, role, places[line].dir);
        }
        auto text = line_text(value, wrapped[to]);
        size_t to_col = core::utf8::count_upto(
            text, index_at(m, text, ta_font_size, *es.goal_x - places[to].x0, role, places[to].dir));
        return at_line_col(to, to_col);
    };

    auto shared = shared_text_event(field, ev, node, es, before, normalized_lines, !field.get().read_only, true);
    if (shared.kind == SharedText::Done) return;
    if (shared.kind == SharedText::Edit) after = shared.edit;
    else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        bool shift = (kp->mods & mods::shift) != 0;
        switch (kp->key) {
        case keys::enter: after = text_edit::insert(before, "\n", field.get().max_length); break;
        case keys::home: after = text_edit::move_to(before, line_col_to_cursor(value, line, 0, wrapped), shift); break;
        case keys::end:
            after = text_edit::move_to(before, wrapped[line].start + wrapped[line].length, shift);
            upstream_after = soft_wrap_after(wrapped, line);
            break;
        case keys::up:
            if (line > 0) after = text_edit::move_to(before, vertical_target(line - 1), shift);
            break;
        case keys::down:
            if (line + 1 < wrapped.size()) after = text_edit::move_to(before, vertical_target(line + 1), shift);
            break;
        default: {
            // simplify: at a line's edge on screen the step falls back to memory order (which
            // moves to the neighbouring line). Upgrade: step to that line's edge on screen.
            auto text = line_text(value, wrapped[line]);
            auto at = core::utf8::offset(text, col);
            auto to = visual_arrow(text, at, before, *kp, m, ta_font_size, role, places[line].dir);
            after = to && *to != at ? text_edit::move_to(before, wrapped[line].start + *to, shift)
                                    : common_edit_key(before, *kp);
        }
        }
    } else {
        after = mouse_edit(before, es, ev, [&](Point p) {
            float rel_y = p.y.raw() - ta_padding_y.raw() + es.scroll_y.raw();
            size_t click_line = static_cast<size_t>(
                std::clamp(rel_y / ta_line_h(m, role).raw(), 0.f, static_cast<float>(wrapped.size() - 1)));
            auto text = line_text(value, wrapped[click_line]);
            size_t click_col = core::utf8::count_upto(
                text, index_at(m, text, ta_font_size, p.x.raw() - ta_padding_x.raw() - places[click_line].x0, role,
                               places[click_line].dir));
            return at_line_col(click_line, click_col);
        });
    }
    // Read-only: the caret and the selection still move (and copy works); a change to the text
    // is dropped, so a cut only copies. (A return, not after.reset(): GCC 14 then warns that the
    // reset optional may be read below, a false maybe-uninitialized.)
    if (after && field.get().read_only && after->text != before.text) return;
    if (after) record_and_apply(field, node, es, before, *after, ev);
    if (vertical) es.goal_cursor = es.cursor;
    if (after) es.upstream = upstream_after;

    if (!after) return;   // only a moved caret or an edit brings the caret into view (not a hover)
    auto new_value = std::string_view(field.get().value.data(), field.get().value.size());
    auto new_wrapped = ui::wrap_lines(new_value, text_area_w, m, ta_font_size, role);
    auto [cur_line, cur_col] = cursor_to_line_col(new_value, es.cursor, new_wrapped, es.upstream);
    Y cursor_y{static_cast<float>(cur_line) * ta_line_h(m, role).raw()};

    if (cursor_y.raw() - es.scroll_y.raw() > text_area_h.raw() - ta_line_h(m, role).raw())
        es.scroll_y = DY{cursor_y.raw() - text_area_h.raw() + ta_line_h(m, role).raw()};
    if (cursor_y.raw() < es.scroll_y.raw())
        es.scroll_y = DY{cursor_y.raw()};
}

} // namespace text_detail

// --- Widget<ComboBox>: a TextField (the same editing code reads value/placeholder/max_length)
// with a list of suggestions in its overlay, below the field. ---

namespace combo_detail {
constexpr float row_h = 24.f;
// simplify: at most this many suggestions, with no scrolling and no "more" hint; scroll the list
// when real option lists outgrow it.
constexpr std::size_t max_rows = 8;

std::string lower(std::string_view s);

// The options containing what was typed (all of them when nothing is), at most max_rows.
// simplify: ASCII case folding; Unicode folding needs the text segmentation dependency (P2).
std::vector<std::size_t> matches(const ComboBox& c);

inline bool list_open(const ComboBox& c, const WidgetNode& node, const TextEditState& es) {
    return node_vs(node).focused && !es.completion_closed && !matches(c).empty();
}

// The row under a point in the widget's coordinates (the list starts below the field).
std::optional<std::size_t> row_at(Point p, std::size_t rows, const WidgetNode& node);

void take(Field<ComboBox>& field, TextEditState& es, std::size_t option);
} // namespace combo_detail

// --- Widget<TextField<T>> method bodies ---

template <StringLike T>
const TextEditState& Widget<TextField<T>>::get_edit_state(const WidgetNode& node) {
    return node.peek<TextEditState>();
}

template <StringLike T>
TextEditState& Widget<TextField<T>>::ensure_edit_state(WidgetNode& node) {
    return node.get_or_create<TextEditState>();
}

template <StringLike T>
void Widget<TextField<T>>::record(DrawList& dl, const Field<TextField<T>>& field,
                                    WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    text_detail::text_field_record(dl, field, node,
        text_detail::plain_display);
}

template <StringLike T>
void Widget<TextField<T>>::handle_input(Field<TextField<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    text_detail::text_field_handle_input(field, ev, node, text_detail::plain_display, true);
}

// --- Widget<Password<T>> method bodies ---

template <StringLike T>
const TextEditState& Widget<Password<T>>::get_edit_state(const WidgetNode& node) {
    return node.peek<TextEditState>();
}

template <StringLike T>
TextEditState& Widget<Password<T>>::ensure_edit_state(WidgetNode& node) {
    return node.get_or_create<TextEditState>();
}

template <StringLike T>
void Widget<Password<T>>::record(DrawList& dl, const Field<Password<T>>& field,
                                    WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    text_detail::text_field_record(dl, field, node,
        text_detail::masked_display);
}

template <StringLike T>
void Widget<Password<T>>::handle_input(Field<Password<T>>& field, const InputEvent& ev,
                                          WidgetNode& node) {
    text_detail::text_field_handle_input(field, ev, node, text_detail::masked_display, false);
}

// --- Widget<TextArea<T>> method bodies ---

template <StringLike T>
const TextAreaEditState& Widget<TextArea<T>>::get_edit_state(const WidgetNode& node) {
    return node.peek<TextAreaEditState>();
}

template <StringLike T>
TextAreaEditState& Widget<TextArea<T>>::ensure_edit_state(WidgetNode& node) {
    return node.get_or_create<TextAreaEditState>();
}

template <StringLike T>
void Widget<TextArea<T>>::record(DrawList& dl, const Field<TextArea<T>>& field,
                                   WidgetNode& node) {
    node.visual_state.cursor = CursorShape::Text;
    text_detail::text_area_record(dl, field, node);
}

template <StringLike T>
void Widget<TextArea<T>>::handle_input(Field<TextArea<T>>& field, const InputEvent& ev,
                                         WidgetNode& node) {
    text_detail::text_area_handle_input(field, ev, node);
}

} // namespace prism::ui
