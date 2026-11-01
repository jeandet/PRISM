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

// Caret x for a byte offset, measured on the text as displayed (the Password mask draws dots).
template <typename DisplayFn>
float caret_x(const TextMeasurer* m, std::string_view value, size_t cursor, DisplayFn display_fn) {
    std::string prefix(value.substr(0, core::utf8::snap(value, cursor)));
    return text_width(m, display_fn(prefix), tf_font_size);
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
        float x0 = caret_x(t.measurer, value, lo, display_fn) - es.scroll_offset.raw();
        float x1 = caret_x(t.measurer, value, hi, display_fn) - es.scroll_offset.raw();
        dl.filled_rect(delegate_detail::make_rect(X{x0}, Y{tf_padding_y.raw()}, Width{x1 - x0},
                                                  tf_widget_h - tf_padding_y * 2.f),
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
        float x0 = caret_x(t.measurer, shown, caret, display_fn) - es.scroll_offset.raw();
        float x1 = caret_x(t.measurer, shown, caret + es.preedit.size(), display_fn) - es.scroll_offset.raw();
        Y y{tf_widget_h.raw() - tf_padding_y.raw() - 3.f};
        dl.line(Point{X{x0}, y}, Point{X{x1}, y}, t.text, 1.f);
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
        return te::move_to(e, core::utf8::prev(e.text, e.cursor), shift);
    case keys::right:
        if (te::has_selection(e) && !shift) return te::move_to(e, hi, false);
        return te::move_to(e, core::utf8::next(e.text, e.cursor), shift);
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

struct LineSpan {
    size_t start;
    size_t length;
};

struct LineCol {
    size_t line;
    size_t col;
};

// TextArea text is fixed-width: columns stay aligned for code and tabular content.
constexpr FontRole ta_font_role = FontRole::Mono;

inline bool is_wrap_space(char c) { return c == ' ' || c == '\t'; }

// Where a visual line ends, given that `fit` bytes of `rest` fit: after the last space or tab
// within the fit, then past any spaces that follow, which hang off the end of the line instead
// of starting the next one (as in browsers and word processors). A word too long for a whole
// line is broken inside it.
inline size_t word_break(std::string_view rest, size_t fit) {
    if (fit >= rest.size()) return rest.size();
    size_t end = fit;
    if (!is_wrap_space(rest[fit])) {
        size_t space = rest.substr(0, fit).find_last_of(" \t");
        if (space != std::string_view::npos) end = space + 1;
    }
    while (end < rest.size() && is_wrap_space(rest[end])) ++end;
    return end;
}

// Greedy word wrap: each visual line takes the longest run of whole words that fits.
// A line's span includes its hanging spaces, so Home/End and the caret count them; the caret
// is drawn at the edge while it is among them.
// simplify: breaks only at spaces and tabs, not after hyphens or between CJK ideographs
// (UAX #14); per code point, not grapheme. Cost: O(log n) measured prefixes per visual line,
// each a distinct string, so very large documents churn the measurer's cache. Upgrade:
// per-character advances (one linear scan).
inline std::vector<LineSpan> wrap_lines(std::string_view text, Width text_area_w,
                                        const TextMeasurer* m, float size) {
    std::vector<LineSpan> result;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        size_t logical_end = (nl == std::string_view::npos) ? text.size() : nl;
        size_t logical_len = logical_end - pos;

        if (logical_len == 0) {
            result.push_back({pos, 0});
        } else {
            size_t offset = 0;
            while (offset < logical_len) {
                auto rest = text.substr(pos + offset, logical_len - offset);
                size_t chunk = word_break(rest, fit_bytes(m, rest, size, text_area_w.raw(), ta_font_role));
                result.push_back({pos + offset, chunk});
                offset += chunk;
            }
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    if (result.empty()) result.push_back({0, 0});
    return result;
}

inline std::string_view line_text(std::string_view text, LineSpan line) {
    return text.substr(std::min(line.start, text.size()), line.length);
}

// `col` is a character column within the wrapped line; cursors are byte offsets.
inline LineCol cursor_to_line_col(std::string_view text, size_t cursor, std::span<const LineSpan> lines) {
    auto at = [&](size_t i) {
        return LineCol{i, core::utf8::count_upto(line_text(text, lines[i]), cursor - lines[i].start)};
    };
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
inline Height ta_line_h(const TextMeasurer* m) {
    return Height{line_height(m, ta_font_size, ta_font_role)};
}
constexpr Width ta_cursor_w{2.f};

template <typename Sentinel>
void text_area_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    const auto& es = node.peek<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(sf.rows) * ta_line_h(m).raw()};
    Height widget_h = ta_padding_y * 2.f + text_area_h;

    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
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
    auto wrapped = wrap_lines(value, text_area_w, m, ta_font_size);

    // Calls fn(x0, x1, line_top) for the part of [lo, hi) on each visual line.
    auto for_each_span = [&](size_t lo, size_t hi, auto fn) {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            size_t a = std::max(lo, wrapped[i].start), b = std::min(hi, wrapped[i].start + wrapped[i].length);
            if (a >= b) continue;
            auto text = line_text(value, wrapped[i]);
            fn(prefix_width(m, text, a - wrapped[i].start, ta_font_size, ta_font_role),
               prefix_width(m, text, b - wrapped[i].start, ta_font_size, ta_font_role),
               Y{static_cast<float>(i) * ta_line_h(m).raw() - es.scroll_y.raw()});
        }
    };

    // Focused or not, as in TextField; not while composing (offsets would not match `shown`).
    if (es.anchor && *es.anchor != es.cursor && es.preedit.empty()) {
        auto [lo, hi] = std::minmax(*es.anchor, es.cursor);
        for_each_span(lo, hi, [&](float x0, float x1, Y y) {
            dl.filled_rect(delegate_detail::make_rect(X{x0}, y, Width{x1 - x0}, ta_line_h(m)), t.text_selection);
        });
    }
    // Highlights (e.g. search matches) are in value offsets: skipped while composing.
    if (es.preedit.empty())
        for (const auto& h : sf.highlights)
            for_each_span(std::min(h.begin, raw.size()), std::min(h.end, raw.size()), [&](float x0, float x1, Y y) {
                dl.filled_rect(delegate_detail::make_rect(X{x0}, y, Width{x1 - x0}, ta_line_h(m)), t.text_match);
            });
    if (!es.preedit.empty()) {   // underline the composition
        for_each_span(caret, caret + es.preedit.size(), [&](float x0, float x1, Y y) {
            Y under = y + DY{ta_line_h(m).raw() - 2.f};
            dl.line(Point{X{x0}, under}, Point{X{x1}, under}, t.text, 1.f);
        });
    }

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, delegate_detail::make_point(X{0}, Y{2}), ta_font_size, t.text_placeholder,
                0.f, TextAnchor::TopLeft, ta_font_role);
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            Y y{static_cast<float>(i) * ta_line_h(m).raw() - es.scroll_y.raw()};
            if (y.raw() + ta_line_h(m).raw() < 0) continue;
            if (y.raw() > text_area_h.raw()) break;
            if (wrapped[i].length > 0) {
                dl.text(std::string(value.substr(wrapped[i].start, wrapped[i].length)), delegate_detail::make_point(X{0}, y + DY{2.f}), ta_font_size, t.text,
                        0.f, TextAnchor::TopLeft, ta_font_role);
            }
        }
    }

    if (vs.focused) {
        size_t shown_caret = caret + (es.preedit.empty() ? 0 : es.preedit_caret);
        auto [line, col] = cursor_to_line_col(value, shown_caret, wrapped);
        auto text = line_text(value, wrapped[line]);
        // Hanging spaces can run past the edge (see word_break): the caret stops at the edge.
        float caret_x = prefix_width(m, text, core::utf8::offset(text, col), ta_font_size, ta_font_role);
        X cx{std::min(caret_x, text_area_w.raw() - ta_cursor_w.raw())};
        Y cy{static_cast<float>(line) * ta_line_h(m).raw() - es.scroll_y.raw()};
        dl.filled_rect(delegate_detail::make_rect(cx, cy, ta_cursor_w, ta_line_h(m)), t.cursor);
    }

    dl.clip_pop();
}

template <typename Sentinel>
void text_area_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = node.get_or_create<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    const TextEdit before = current_edit(field, es);
    std::string_view value = before.text;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(field.get().rows) * ta_line_h(m).raw()};
    auto wrapped = wrap_lines(value, text_area_w, m, ta_font_size);
    auto [line, col] = cursor_to_line_col(value, before.cursor, wrapped);
    std::optional<TextEdit> after;

    if (auto* co = std::get_if<Composition>(&ev)) {
        set_composition(es, node, *co);
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
            break;
        case keys::up:
            if (line > 0) after = text_edit::move_to(before, line_col_to_cursor(value, line - 1, col, wrapped), shift);
            break;
        case keys::down:
            if (line + 1 < wrapped.size())
                after = text_edit::move_to(before, line_col_to_cursor(value, line + 1, col, wrapped), shift);
            break;
        default: after = common_edit_key(before, *kp);
        }
    } else {
        after = mouse_edit(before, es, ev, [&](Point p) {
            float rel_y = p.y.raw() - ta_padding_y.raw() + es.scroll_y.raw();
            size_t click_line = static_cast<size_t>(
                std::clamp(rel_y / ta_line_h(m).raw(), 0.f, static_cast<float>(wrapped.size() - 1)));
            auto text = line_text(value, wrapped[click_line]);
            size_t click_col = core::utf8::count_upto(
                text, index_at(m, text, ta_font_size, p.x.raw() - ta_padding_x.raw(), ta_font_role));
            return line_col_to_cursor(value, click_line, click_col, wrapped);
        });
    }
    if (after) {
        es.history.record(before, *after, std::holds_alternative<TextInput>(ev)
                                              ? EditHistory::Kind::Typing : EditHistory::Kind::Other);
        apply_edit(field, node, es, before, *after);
    }

    auto new_value = std::string_view(field.get().value.data(), field.get().value.size());
    auto new_wrapped = wrap_lines(new_value, text_area_w, m, ta_font_size);
    auto [cur_line, cur_col] = cursor_to_line_col(new_value, es.cursor, new_wrapped);
    Y cursor_y{static_cast<float>(cur_line) * ta_line_h(m).raw()};

    if (cursor_y.raw() - es.scroll_y.raw() > text_area_h.raw() - ta_line_h(m).raw())
        es.scroll_y = DY{cursor_y.raw() - text_area_h.raw() + ta_line_h(m).raw()};
    if (cursor_y.raw() < es.scroll_y.raw())
        es.scroll_y = DY{cursor_y.raw()};
}

} // namespace text_detail

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
