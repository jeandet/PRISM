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

inline std::string mask_string(size_t len) {
    std::string result;
    result.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
        result += "\xe2\x97\x8f";
    return result;
}

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
inline std::string with_preedit(std::string_view value, std::size_t cursor, std::string_view preedit) {
    std::string out(value.substr(0, cursor));
    out += preedit;
    out += value.substr(cursor);
    return out;
}

inline std::string plain_display(const std::string& v) { return v; }
inline std::string masked_display(const std::string& v) { return mask_string(core::utf8::count(v)); }

template <typename Sentinel, typename DisplayFn>
void text_field_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node,
                       DisplayFn display_fn) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    const auto& es = node.peek<TextEditState>();
    Width w = delegate_detail::widget_w(node);

    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, tf_widget_h), bg);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, tf_widget_h, t);

    Width text_area_w = w - tf_padding_x * 2.f;
    dl.clip_push(delegate_detail::make_point(X{tf_padding_x.raw()}, Y{0}), Size{text_area_w, tf_widget_h});

    auto value = std::string_view(sf.value.data(), sf.value.size());
    size_t caret = core::utf8::snap(value, es.cursor);
    // What is drawn: the value, with an input-method preedit inserted at the caret.
    std::string shown = es.preedit.empty() ? std::string(value) : with_preedit(value, caret, es.preedit);

    // Drawn focused or not: a selection that is invisible but still replaced by the next
    // keystroke would lose text. (Not while composing: offsets would not match `shown`.)
    if (es.anchor && *es.anchor != es.cursor && es.preedit.empty()) {
        auto [lo, hi] = std::minmax(*es.anchor, es.cursor);
        for (auto [x0, x1] : range_spans(t.measurer, value, lo, hi, display_fn))
            dl.filled_rect(delegate_detail::make_rect(X{x0 - es.scroll_offset.raw()}, Y{tf_padding_y.raw()},
                                                      Width{x1 - x0}, tf_widget_h - tf_padding_y * 2.f),
                           t.text_selection);
    }

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, delegate_detail::make_point(X{0}, Y{tf_padding_y.raw() + 2.f}), tf_font_size,
                t.text_placeholder);
    } else {
        X text_x{-es.scroll_offset.raw()};
        dl.text(display_fn(shown), delegate_detail::make_point(text_x, Y{tf_padding_y.raw() + 2.f}), tf_font_size,
                t.text);
    }

    if (!es.preedit.empty()) {   // underline the composition
        Y y{tf_widget_h.raw() - tf_padding_y.raw() - 3.f};
        for (auto [x0, x1] : range_spans(t.measurer, shown, caret, caret + es.preedit.size(), display_fn))
            dl.line(Point{X{x0 - es.scroll_offset.raw()}, y}, Point{X{x1 - es.scroll_offset.raw()}, y}, t.text, 1.f);
    }

    if (vs.focused) {
        size_t shown_caret = caret + (es.preedit.empty() ? 0 : es.preedit_caret);
        X cursor_x{caret_x(t.measurer, shown, shown_caret, display_fn) - es.scroll_offset.raw()};
        dl.filled_rect(delegate_detail::make_rect(cursor_x, Y{tf_padding_y.raw()}, tf_cursor_w,
                                 tf_widget_h - tf_padding_y * 2.f),
                       t.cursor);
    }

    dl.clip_pop();
}

// Keys shared by TextField and TextArea, as edits on a TextEdit. Returns nullopt for keys the
// widget handles itself (home/end, up/down, enter).
inline std::optional<TextEdit> common_edit_key(const TextEdit& e, const KeyPress& kp) {
    namespace te = text_edit;
    bool shift = (kp.mods & mods::shift) != 0;
    auto [lo, hi] = te::selection(e);
    switch (kp.key) {
    case keys::a: if (mods::shortcut(kp.mods)) return te::select_all(e); break;
    case keys::backspace: return te::erase_back(e);
    case keys::delete_: return te::erase_forward(e);
    case keys::left:
        if (te::has_selection(e) && !shift) return te::move_to(e, lo, false);
        return te::move_to(e, prev_grapheme(e.text, e.cursor), shift);
    case keys::right:
        if (te::has_selection(e) && !shift) return te::move_to(e, hi, false);
        return te::move_to(e, next_grapheme(e.text, e.cursor), shift);
    }
    return std::nullopt;
}

// Ctrl/Cmd+C and +X. Returns nullopt for other keys. Without a clipboard, a selection, or
// permission (secret fields), the key is consumed and nothing happens -- in particular cut never
// deletes text it could not put on the clipboard.
inline std::optional<TextEdit> clipboard_key(const TextEdit& e, const KeyPress& kp,
                                             const Clipboard* cb, bool allow_copy) {
    if (!mods::shortcut(kp.mods) || (kp.key != keys::c && kp.key != keys::x)) return std::nullopt;
    if (!cb || !allow_copy || !text_edit::has_selection(e)) return e;
    cb->set_text(std::string(text_edit::selected(e)));
    return kp.key == keys::x ? text_edit::erase_back(e) : e;
}

// Pasted text for a single-line field: line breaks become spaces.
inline std::string single_line(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;   // CRLF -> one space
        out += (s[i] == '\r' || s[i] == '\n') ? ' ' : s[i];
    }
    return out;
}

// Pasted text for a multi-line area: Windows/old-Mac line ends become '\n'.
inline std::string normalized_lines(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') { if (i + 1 >= s.size() || s[i + 1] != '\n') out += '\n'; }
        else out += s[i];
    }
    return out;
}

// Ctrl/Cmd+Z undoes; Ctrl/Cmd+Shift+Z and Ctrl/Cmd+Y redo. nullopt for other keys.
inline std::optional<std::optional<TextEdit>> history_key(EditHistory& h, const TextEdit& e, const KeyPress& kp) {
    if (!mods::shortcut(kp.mods)) return std::nullopt;
    bool shift = (kp.mods & mods::shift) != 0;
    if (kp.key == keys::z) return shift ? h.redo(e) : h.undo(e);
    if (kp.key == keys::y) return h.redo(e);
    return std::nullopt;
}

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

// allow_copy: false for secret fields (Password), which must never reach the clipboard.
template <typename Sentinel, typename DisplayFn>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node,
                             DisplayFn display_fn, bool allow_copy) {
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    const Clipboard* cb = node.theme ? node.theme->clipboard : nullptr;
    auto& es = node.get_or_create<TextEditState>();
    const TextEdit before = current_edit(field, es);
    std::optional<TextEdit> after;

    if (auto* co = std::get_if<Composition>(&ev)) {
        if (allow_copy) set_composition(es, node, *co);   // a secret field never shows plaintext
        return;
    }
    if (std::holds_alternative<TextInput>(ev) || std::holds_alternative<Paste>(ev)) es.preedit.clear();

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        after = text_edit::insert(before, ti->text, field.get().max_length);
    } else if (auto* pa = std::get_if<Paste>(&ev)) {
        after = text_edit::insert(before, single_line(pa->text), field.get().max_length);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        bool shift = (kp->mods & mods::shift) != 0;
        if (auto h = history_key(es.history, before, *kp)) {
            if (*h) apply_edit(field, node, es, before, **h);
            return;   // an undo step is not itself recorded
        }
        if (auto c = clipboard_key(before, *kp, cb, allow_copy)) after = c;
        else if (kp->key == keys::home) after = text_edit::move_to(before, 0, shift);
        else if (kp->key == keys::end) after = text_edit::move_to(before, before.text.size(), shift);
        else after = common_edit_key(before, *kp);
    } else {
        after = mouse_edit(before, es, ev, [&](Point p) {
            return cursor_at(m, before.text, p.x.raw() - tf_padding_x.raw() + es.scroll_offset.raw(), display_fn);
        });
    }
    if (after) {
        es.history.record(before, *after, std::holds_alternative<TextInput>(ev)
                                              ? EditHistory::Kind::Typing : EditHistory::Kind::Other);
        apply_edit(field, node, es, before, *after);
    }

    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - tf_padding_x * 2.f;
    auto now = std::string_view(field.get().value.data(), field.get().value.size());
    Width cursor_px{caret_x(m, now, es.cursor, display_fn)};
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
inline std::string drawn_line(std::string_view text, TextDirection dir) {
    if (text.empty() || paragraph_direction(text) == dir) return std::string(text);
    return (dir == TextDirection::Rtl ? "\xE2\x80\x8F" : "\xE2\x80\x8E") + std::string(text);
}

// `col` is a character column within the wrapped line; cursors are byte offsets.
// Whether line i ends where the next begins: a wrap inside a paragraph, not a newline.
inline bool soft_wrap_after(std::span<const LineSpan> lines, size_t i) {
    return i + 1 < lines.size() && lines[i + 1].start == lines[i].start + lines[i].length;
}

// Each wrapped line's place. A paragraph's direction is worked out once, at its first line.
inline std::vector<LinePlace> place_lines(std::string_view value, std::span<const LineSpan> lines, Width area_w,
                                          const TextMeasurer* m, float size, FontRole role) {
    std::vector<LinePlace> places;
    places.reserve(lines.size());
    TextDirection dir = TextDirection::Ltr;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 0 || !soft_wrap_after(lines, i - 1)) dir = paragraph_direction(paragraph_from(value, lines[i]));
        float x0 = 0.f;
        if (dir == TextDirection::Rtl) {
            auto text = value.substr(std::min(lines[i].start, value.size()), lines[i].length);
            while (!text.empty() && is_wrap_space(text.back())) text.remove_suffix(1);   // hanging spaces
            x0 = std::max(0.f, area_w.raw() - text_width(m, text, size, role));
        }
        places.push_back({dir, x0});
    }
    return places;
}

// `upstream`: at a soft wrap, the caret is at the end of the line above (see
// TextAreaEditState::upstream), not at the start of the next.
inline LineCol cursor_to_line_col(std::string_view text, size_t cursor, std::span<const LineSpan> lines,
                                  bool upstream = false) {
    auto at = [&](size_t i) {
        return LineCol{i, core::utf8::count_upto(line_text(text, lines[i]), cursor - lines[i].start)};
    };
    if (upstream)
        for (size_t i = 0; i < lines.size(); ++i)
            if (soft_wrap_after(lines, i) && lines[i + 1].start == cursor) return at(i);
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t line_end = lines[i].start + lines[i].length;
        bool is_last = (i + 1 == lines.size());
        if (cursor < line_end || (cursor == line_end && is_last))
            return at(i);
        if (!is_last && lines[i + 1].start > line_end) {
            if (cursor <= line_end)
                return at(i);
        }
    }
    auto& last = lines.back();
    return {lines.size() - 1, core::utf8::count(line_text(text, last))};
}

inline size_t line_col_to_cursor(std::string_view text, size_t line, size_t col,
                                 std::span<const LineSpan> lines) {
    if (line >= lines.size()) line = lines.size() - 1;
    return lines[line].start + core::utf8::offset(line_text(text, lines[line]), col);
}

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
inline void publish_scroll(WidgetNode& node, TextAreaEditState& es, Height content_h, Width text_area_w,
                           Height text_area_h) {
    es.viewport_h = text_area_h;
    es.content_h = content_h;
    es.scroll_y = DY{std::clamp(es.scroll_y.raw(), 0.f, std::max(0.f, content_h.raw() - text_area_h.raw()))};
    Rect viewport{Point{X{ta_padding_x.raw()}, Y{ta_padding_y.raw()}}, Size{text_area_w, text_area_h}};
    if (content_h.raw() > text_area_h.raw()) node.inner_scroll = WidgetNode::InnerScroll{viewport, content_h, es.scroll_y};
    else node.inner_scroll.reset();
}

template <typename Sentinel>
void text_area_record(DrawList& dl, const Field<Sentinel>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    auto& es = node.get_or_create<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    const FontRole role = field.get().font;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(sf.rows) * ta_line_h(m, role).raw()};
    Height widget_h = ta_padding_y * 2.f + text_area_h;

    auto& t = *node.theme;
    // Read-only text reads as plain text: its box is clear (it still sizes the widget).
    auto bg = sf.read_only ? Color::rgba(0, 0, 0, 0)
            : vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, widget_h), bg);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, widget_h, t);

    dl.clip_push(delegate_detail::make_point(X{ta_padding_x.raw()}, Y{ta_padding_y.raw()}), Size{text_area_w, text_area_h});

    auto raw = std::string_view(sf.value.data(), sf.value.size());
    size_t caret = core::utf8::snap(raw, es.cursor);
    // What is drawn: the value, with an input-method preedit inserted at the caret.
    std::string shown = es.preedit.empty() ? std::string(raw) : with_preedit(raw, caret, es.preedit);
    std::string_view value = shown;
    auto wrapped = ui::wrap_lines(value, text_area_w, m, ta_font_size, role);
    publish_scroll(node, es, Height{static_cast<float>(wrapped.size()) * ta_line_h(m, role).raw()}, text_area_w, text_area_h);

    // Calls fn(x0, x1, line_top) for the part of [lo, hi) on each visual line: several calls for
    // one line when the range crosses a direction change there.
    auto places = place_lines(value, wrapped, text_area_w, m, ta_font_size, role);
    auto for_each_span = [&](size_t lo, size_t hi, auto fn) {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            size_t a = std::max(lo, wrapped[i].start), b = std::min(hi, wrapped[i].start + wrapped[i].length);
            if (a >= b) continue;
            auto text = line_text(value, wrapped[i]);
            Y y{static_cast<float>(i) * ta_line_h(m, role).raw() - es.scroll_y.raw()};
            for (auto [x0, x1] : selection_spans(m, text, a - wrapped[i].start, b - wrapped[i].start,
                                                 ta_font_size, role, places[i].dir))
                fn(places[i].x0 + x0, places[i].x0 + x1, y);
        }
    };

    // Focused or not, as in TextField; not while composing (offsets would not match `shown`).
    if (es.anchor && *es.anchor != es.cursor && es.preedit.empty()) {
        auto [lo, hi] = std::minmax(*es.anchor, es.cursor);
        for_each_span(lo, hi, [&](float x0, float x1, Y y) {
            dl.filled_rect(delegate_detail::make_rect(X{x0}, y, Width{x1 - x0}, ta_line_h(m, role)), t.text_selection);
        });
    }
    // Highlights (e.g. search matches) are in value offsets: skipped while composing.
    if (es.preedit.empty())
        for (const auto& h : sf.highlights)
            for_each_span(std::min(h.begin, raw.size()), std::min(h.end, raw.size()), [&](float x0, float x1, Y y) {
                dl.filled_rect(delegate_detail::make_rect(X{x0}, y, Width{x1 - x0}, ta_line_h(m, role)), t.text_match);
            });
    if (!es.preedit.empty()) {   // underline the composition
        for_each_span(caret, caret + es.preedit.size(), [&](float x0, float x1, Y y) {
            Y under = y + DY{ta_line_h(m, role).raw() - 2.f};
            dl.line(Point{X{x0}, under}, Point{X{x1}, under}, t.text, 1.f);
        });
    }

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, delegate_detail::make_point(X{0}, Y{2}), ta_font_size, t.text_placeholder,
                0.f, TextAnchor::TopLeft, role);
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            Y y{static_cast<float>(i) * ta_line_h(m, role).raw() - es.scroll_y.raw()};
            if (y.raw() + ta_line_h(m, role).raw() < 0) continue;
            if (y.raw() > text_area_h.raw()) break;
            if (wrapped[i].length > 0) {
                dl.text(drawn_line(line_text(value, wrapped[i]), places[i].dir),
                        delegate_detail::make_point(X{places[i].x0}, y + DY{2.f}), ta_font_size, t.text,
                        0.f, TextAnchor::TopLeft, role);
            }
        }
    }

    if (vs.focused) {
        size_t shown_caret = caret + (es.preedit.empty() ? 0 : es.preedit_caret);
        auto [line, col] = cursor_to_line_col(value, shown_caret, wrapped, es.upstream && es.preedit.empty());
        auto text = line_text(value, wrapped[line]);
        // Hanging spaces can run past the edge (see word_break): the caret stops at the edge.
        float caret_x = places[line].x0
                      + caret_x_at(m, text, core::utf8::offset(text, col), ta_font_size, role, places[line].dir);
        X cx{std::min(caret_x, text_area_w.raw() - ta_cursor_w.raw())};
        Y cy{static_cast<float>(line) * ta_line_h(m, role).raw() - es.scroll_y.raw()};
        dl.filled_rect(delegate_detail::make_rect(cx, cy, ta_cursor_w, ta_line_h(m, role)), t.cursor);
    }

    dl.clip_pop();
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

    if (auto* co = std::get_if<Composition>(&ev)) {
        if (!field.get().read_only) set_composition(es, node, *co);
        return;
    }
    if (std::holds_alternative<TextInput>(ev) || std::holds_alternative<Paste>(ev)) es.preedit.clear();

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        after = text_edit::insert(before, ti->text, field.get().max_length);
    } else if (auto* pa = std::get_if<Paste>(&ev)) {
        after = text_edit::insert(before, normalized_lines(pa->text), field.get().max_length);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        bool shift = (kp->mods & mods::shift) != 0;
        if (auto h = history_key(es.history, before, *kp)) {
            if (*h) apply_edit(field, node, es, before, **h);
            return;   // an undo step is not itself recorded
        }
        if (auto c = clipboard_key(before, *kp, node.theme ? node.theme->clipboard : nullptr, true)) after = c;
        else switch (kp->key) {
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
        default: after = common_edit_key(before, *kp);
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
    if (after) {
        es.history.record(before, *after, std::holds_alternative<TextInput>(ev)
                                              ? EditHistory::Kind::Typing : EditHistory::Kind::Other);
        apply_edit(field, node, es, before, *after);
    }
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

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The options containing what was typed (all of them when nothing is), at most max_rows.
// simplify: ASCII case folding; Unicode folding needs the text segmentation dependency (P2).
inline std::vector<std::size_t> matches(const ComboBox& c) {
    std::vector<std::size_t> out;
    auto typed = lower(c.value);
    for (std::size_t i = 0; i < c.options.size() && out.size() < max_rows; ++i)
        if (lower(c.options[i]).find(typed) != std::string::npos) out.push_back(i);
    return out;
}

inline bool list_open(const ComboBox& c, const WidgetNode& node, const TextEditState& es) {
    return node_vs(node).focused && !es.completion_closed && !matches(c).empty();
}

// The row under a point in the widget's coordinates (the list starts below the field).
inline std::optional<std::size_t> row_at(Point p, std::size_t rows, const WidgetNode& node) {
    float top = Widget<TextField<>>::widget_h.raw();
    if (p.y.raw() < top || p.x.raw() < 0 || p.x.raw() > delegate_detail::widget_w(node).raw()) return std::nullopt;
    auto i = static_cast<std::size_t>((p.y.raw() - top) / row_h);
    return i < rows ? std::optional{i} : std::nullopt;
}

inline void take(Field<ComboBox>& field, TextEditState& es, std::size_t option) {
    auto c = field.get();
    c.value = c.options[option];
    es.cursor = c.value.size();
    es.anchor.reset();
    es.completion_closed = true;
    field.set(c);
}
} // namespace combo_detail

inline void Widget<ComboBox>::record(DrawList& dl, const Field<ComboBox>& field, WidgetNode& node) {
    using namespace combo_detail;
    node.visual_state.cursor = CursorShape::Text;
    text_detail::text_field_record(dl, field, node, text_detail::plain_display);
    auto& es = node.get_or_create<TextEditState>();
    if (!list_open(field.get(), node, es)) return;
    auto rows = matches(field.get());
    es.completion_index = std::min(es.completion_index, rows.size() - 1);
    const auto& t = node_theme(node);
    float w = delegate_detail::widget_w(node).raw();
    float top = Widget<TextField<>>::widget_h.raw();
    Rect list{Point{X{0}, Y{top}}, Size{Width{w}, Height{row_h * static_cast<float>(rows.size())}}};
    node.overlay_draws.filled_rect(list, t.popup_bg);
    node.overlay_draws.rect_outline(list, t.popup_border, 1.f);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        Y y{top + row_h * static_cast<float>(i)};
        bool hl = i == es.completion_index;
        if (hl) node.overlay_draws.filled_rect(Rect{Point{X{0}, y}, Size{Width{w}, Height{row_h}}}, t.popup_highlight);
        node.overlay_draws.text(field.get().options[rows[i]], Point{X{8.f}, y + DY{4.f}}, 14.f,
                                hl ? t.text_on_primary : t.text);
    }
}

inline void Widget<ComboBox>::handle_input(Field<ComboBox>& field, const InputEvent& ev, WidgetNode& node) {
    using namespace combo_detail;
    auto& es = node.get_or_create<TextEditState>();
    // A press on the field or Down brings a closed list back (as Qt's combo box does).
    auto* down = std::get_if<KeyPress>(&ev);
    bool reopen = (down && down->key == keys::down && es.completion_closed)
               || (std::holds_alternative<MouseButton>(ev) && std::get<MouseButton>(ev).pressed && es.completion_closed
                   && !row_at(std::get<MouseButton>(ev).position, 0, node)
                   && std::get<MouseButton>(ev).position.y.raw() < Widget<TextField<>>::widget_h.raw());
    if (reopen) {
        es.completion_closed = false;
        es.completion_index = 0;
        node.dirty = true;
        if (down) return;
    }
    if (list_open(field.get(), node, es)) {
        auto rows = matches(field.get());
        if (auto* kp = std::get_if<KeyPress>(&ev)) {
            if (kp->key == keys::down || kp->key == keys::up) {
                bool down = kp->key == keys::down;
                es.completion_index = down ? std::min(es.completion_index + 1, rows.size() - 1)
                                           : (es.completion_index > 0 ? es.completion_index - 1 : 0);
                node.dirty = true;
                return;
            }
            if (kp->key == keys::enter) return take(field, es, rows[std::min(es.completion_index, rows.size() - 1)]);
            if (kp->key == keys::escape) {
                es.completion_closed = true;
                node.dirty = true;
                return;
            }
        }
        if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left)
            if (auto i = row_at(mb->position, rows.size(), node)) return take(field, es, rows[*i]);
    }
    std::string before = field.get().value;
    text_detail::text_field_handle_input(field, ev, node, text_detail::plain_display, true);
    if (field.get().value != before) {   // typing reopens the list at its top
        auto& after = node.get_or_create<TextEditState>();
        after.completion_closed = false;
        after.completion_index = 0;
        node.dirty = true;
    }
}

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
