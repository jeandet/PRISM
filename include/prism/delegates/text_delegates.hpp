#pragma once

#include <prism/core/utf8.hpp>
#include <prism/ui/text_metrics.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cassert>
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

// Truncates `text` so `current` + result stays within `max_length` characters (0 = no limit),
// never cutting a character in half.
inline std::string clamp_to_max_length(std::string text, std::string_view current, size_t max_length) {
    if (max_length == 0) return text;
    size_t have = core::utf8::count(current);
    size_t room = have < max_length ? max_length - have : 0;
    text.resize(core::utf8::offset(text, room));
    return text;
}

template <typename Sentinel>
void set_erased(Field<Sentinel>& field, Sentinel sf, std::string_view value, size_t from, size_t to) {
    std::string v(value);
    v.erase(from, to - from);
    sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
    field.set(sf);
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

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, delegate_detail::make_point(X{0}, Y{tf_padding_y.raw() + 2.f}), tf_font_size,
                t.text_placeholder);
    } else {
        X text_x{-es.scroll_offset.raw()};
        std::string display_text = display_fn(std::string(sf.value.data(), sf.value.size()));
        dl.text(display_text, delegate_detail::make_point(text_x, Y{tf_padding_y.raw() + 2.f}), tf_font_size,
                t.text);
    }

    if (vs.focused) {
        auto value = std::string_view(sf.value.data(), sf.value.size());
        X cursor_x{caret_x(t.measurer, value, es.cursor, display_fn) - es.scroll_offset.raw()};
        dl.filled_rect(delegate_detail::make_rect(cursor_x, Y{tf_padding_y.raw()}, tf_cursor_w,
                                 tf_widget_h - tf_padding_y * 2.f),
                       t.cursor);
    }

    dl.clip_pop();
}

template <typename Sentinel, typename DisplayFn>
void text_field_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node,
                             DisplayFn display_fn) {
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    auto& es = node.get_or_create<TextEditState>();
    auto sf = field.get();
    auto value = std::string_view(sf.value.data(), sf.value.size());
    auto len = value.size();
    es.cursor = core::utf8::snap(value, es.cursor);

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = clamp_to_max_length(ti->text, value, sf.max_length);
        if (!to_insert.empty()) {
            std::string v(value);
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::backspace && es.cursor > 0) {
            set_erased(field, sf, value, core::utf8::prev(value, es.cursor), es.cursor);
            es.cursor = core::utf8::prev(value, es.cursor);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            set_erased(field, sf, value, es.cursor, core::utf8::next(value, es.cursor));
        } else if (kp->key == keys::left && es.cursor > 0) {
            es.cursor = core::utf8::prev(value, es.cursor);
            node.dirty = true;
        } else if (kp->key == keys::right && es.cursor < len) {
            es.cursor = core::utf8::next(value, es.cursor);
            node.dirty = true;
        } else if (kp->key == keys::home && es.cursor > 0) {
            es.cursor = 0;
            node.dirty = true;
        } else if (kp->key == keys::end && es.cursor < len) {
            es.cursor = len;
            node.dirty = true;
        }
    } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float rel_x = mb->position.x.raw() - tf_padding_x.raw() + es.scroll_offset.raw();
        size_t pos = cursor_at(m, value, rel_x, display_fn);
        if (pos != es.cursor) {
            es.cursor = pos;
            node.dirty = true;
        }
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

// Greedy wrap: each visual line takes the longest prefix that fits text_area_w.
// simplify: breaks anywhere, not at word boundaries (UAX #14); per code point, not grapheme.
// Cost: O(log n) measured prefixes per visual line, each a distinct string, so very large
// documents churn the measurer's cache. Upgrade: per-character advances (one linear scan).
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
                size_t chunk = fit_bytes(m, rest, size, text_area_w.raw());
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
constexpr Height ta_line_height{ta_font_size * 1.4f};
constexpr Width ta_cursor_w{2.f};

template <typename Sentinel>
void text_area_record(DrawList& dl, const Field<Sentinel>& field, const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& sf = field.get();
    const auto& es = node.peek<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(sf.rows) * ta_line_height.raw()};
    Height widget_h = ta_padding_y * 2.f + text_area_h;

    auto& t = *node.theme;
    auto bg = vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, widget_h), bg);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, widget_h, t);

    dl.clip_push(delegate_detail::make_point(X{ta_padding_x.raw()}, Y{ta_padding_y.raw()}), Size{text_area_w, text_area_h});

    auto value = std::string_view(sf.value.data(), sf.value.size());
    auto wrapped = wrap_lines(value, text_area_w, m, ta_font_size);

    if (sf.value.empty() && !vs.focused) {
        dl.text(sf.placeholder, delegate_detail::make_point(X{0}, Y{2}), ta_font_size, t.text_placeholder);
    } else {
        for (size_t i = 0; i < wrapped.size(); ++i) {
            Y y{static_cast<float>(i) * ta_line_height.raw() - es.scroll_y.raw()};
            if (y.raw() + ta_line_height.raw() < 0) continue;
            if (y.raw() > text_area_h.raw()) break;
            if (wrapped[i].length > 0) {
                std::string line_text(sf.value.data() + wrapped[i].start, wrapped[i].length);
                dl.text(line_text, delegate_detail::make_point(X{0}, y + DY{2.f}), ta_font_size, t.text);
            }
        }
    }

    if (vs.focused) {
        auto [line, col] = cursor_to_line_col(std::string_view(sf.value.data(), sf.value.size()),
                                              es.cursor, wrapped);
        auto text = line_text(value, wrapped[line]);
        X cx{prefix_width(m, text, core::utf8::offset(text, col), ta_font_size)};
        Y cy{static_cast<float>(line) * ta_line_height.raw() - es.scroll_y.raw()};
        dl.filled_rect(delegate_detail::make_rect(cx, cy, ta_cursor_w, ta_line_height), t.cursor);
    }

    dl.clip_pop();
}

template <typename Sentinel>
void text_area_handle_input(Field<Sentinel>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = node.get_or_create<TextAreaEditState>();
    auto sf = field.get();
    auto value = std::string_view(sf.value.data(), sf.value.size());
    auto len = value.size();
    es.cursor = core::utf8::snap(value, es.cursor);
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(sf.rows) * ta_line_height.raw()};

    auto wrapped = wrap_lines(value, text_area_w, m, ta_font_size);

    if (auto* ti = std::get_if<TextInput>(&ev)) {
        std::string to_insert = clamp_to_max_length(ti->text, value, sf.max_length);
        if (!to_insert.empty()) {
            std::string v(value);
            v.insert(es.cursor, to_insert);
            es.cursor += to_insert.size();
            sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
            field.set(sf);
        }
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if (kp->key == keys::enter) {
            if (sf.max_length == 0 || core::utf8::count(value) < sf.max_length) {
                std::string v(sf.value.data(), sf.value.size());
                v.insert(es.cursor, 1, '\n');
                es.cursor++;
                sf.value = std::remove_cvref_t<decltype(sf.value)>(v);
                field.set(sf);
            }
        } else if (kp->key == keys::backspace && es.cursor > 0) {
            set_erased(field, sf, value, core::utf8::prev(value, es.cursor), es.cursor);
            es.cursor = core::utf8::prev(value, es.cursor);
        } else if (kp->key == keys::delete_ && es.cursor < len) {
            set_erased(field, sf, value, es.cursor, core::utf8::next(value, es.cursor));
        } else if (kp->key == keys::left && es.cursor > 0) {
            es.cursor = core::utf8::prev(value, es.cursor);
            node.dirty = true;
        } else if (kp->key == keys::right && es.cursor < len) {
            es.cursor = core::utf8::next(value, es.cursor);
            node.dirty = true;
        } else if (kp->key == keys::home) {
            auto [line, col] = cursor_to_line_col(value, es.cursor, wrapped);
            size_t new_cursor = line_col_to_cursor(value, line, 0, wrapped);
            if (new_cursor != es.cursor) {
                es.cursor = new_cursor;
                node.dirty = true;
            }
        } else if (kp->key == keys::end) {
            auto [line, col] = cursor_to_line_col(value, es.cursor, wrapped);
            size_t new_cursor = wrapped[line].start + wrapped[line].length;
            if (new_cursor != es.cursor) {
                es.cursor = new_cursor;
                node.dirty = true;
            }
        } else if (kp->key == keys::up) {
            auto [line, col] = cursor_to_line_col(value, es.cursor, wrapped);
            if (line > 0) {
                es.cursor = line_col_to_cursor(value, line - 1, col, wrapped);
                node.dirty = true;
            }
        } else if (kp->key == keys::down) {
            auto [line, col] = cursor_to_line_col(value, es.cursor, wrapped);
            if (line + 1 < wrapped.size()) {
                es.cursor = line_col_to_cursor(value, line + 1, col, wrapped);
                node.dirty = true;
            }
        }
    } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed) {
        float rel_y = mb->position.y.raw() - ta_padding_y.raw() + es.scroll_y.raw();
        size_t click_line = static_cast<size_t>(
            std::clamp(rel_y / ta_line_height.raw(), 0.f, static_cast<float>(wrapped.size() - 1)));
        float rel_x = mb->position.x.raw() - ta_padding_x.raw();
        auto text = line_text(value, wrapped[click_line]);
        size_t click_col = core::utf8::count_upto(text, index_at(m, text, ta_font_size, rel_x));
        size_t new_cursor = line_col_to_cursor(value, click_line, click_col, wrapped);
        if (new_cursor != es.cursor) {
            es.cursor = new_cursor;
            node.dirty = true;
        }
    }

    auto new_value = std::string_view(field.get().value.data(), field.get().value.size());
    auto new_wrapped = wrap_lines(new_value, text_area_w, m, ta_font_size);
    auto [cur_line, cur_col] = cursor_to_line_col(new_value, es.cursor, new_wrapped);
    Y cursor_y{static_cast<float>(cur_line) * ta_line_height.raw()};

    if (cursor_y.raw() - es.scroll_y.raw() > text_area_h.raw() - ta_line_height.raw())
        es.scroll_y = DY{cursor_y.raw() - text_area_h.raw() + ta_line_height.raw()};
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
    text_detail::text_field_handle_input(field, ev, node, text_detail::plain_display);
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
    text_detail::text_field_handle_input(field, ev, node, text_detail::masked_display);
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
