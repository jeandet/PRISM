#include <prism/delegates/text_delegates.hpp>

#include <prism/core/utf8.hpp>
#include <prism/ui/clipboard.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prism::ui {

namespace text_detail {

std::string mask_string(size_t len) {
    std::string result;
    result.reserve(len * 3);
    for (size_t i = 0; i < len; ++i)
        result += "\xe2\x97\x8f";
    return result;
}

std::string with_preedit(std::string_view value, std::size_t cursor, std::string_view preedit) {
    std::string out(value.substr(0, cursor));
    out += preedit;
    out += value.substr(cursor);
    return out;
}

void draw_text_field(DrawList& dl, const TextFieldDraw& field, const WidgetNode& node) {
    auto& vs = node_vs(node);
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

    auto value = std::string_view(field.value.data(), field.value.size());
    size_t caret = core::utf8::snap(value, es.cursor);
    // What is drawn: the value, with an input-method preedit inserted at the caret.
    std::string shown = es.preedit.empty() ? std::string(value) : with_preedit(value, caret, es.preedit);
    // Where the text starts on screen: right-aligned right-to-left text, minus the scroll.
    float shift = text_x0(t.measurer, shown, text_area_w, field.display) - es.scroll_offset.raw();

    // Drawn focused or not: a selection that is invisible but still replaced by the next
    // keystroke would lose text. (Not while composing: offsets would not match `shown`.)
    if (es.anchor && *es.anchor != es.cursor && es.preedit.empty()) {
        auto [lo, hi] = std::minmax(*es.anchor, es.cursor);
        for (auto [x0, x1] : range_spans(t.measurer, value, lo, hi, field.display))
            dl.filled_rect(delegate_detail::make_rect(X{x0 + shift}, Y{tf_padding_y.raw()},
                                                      Width{x1 - x0}, tf_widget_h - tf_padding_y * 2.f),
                           t.text_selection);
    }

    if (field.value.empty() && !vs.focused) {
        dl.text(std::string(field.placeholder), delegate_detail::make_point(X{0}, Y{tf_padding_y.raw() + 2.f}),
                tf_font_size, t.text_placeholder);
    } else {
        X text_x{shift};
        dl.text(field.display(shown), delegate_detail::make_point(text_x, Y{tf_padding_y.raw() + 2.f}), tf_font_size,
                t.text);
    }

    if (!es.preedit.empty()) {   // underline the composition
        Y y{tf_widget_h.raw() - tf_padding_y.raw() - 3.f};
        for (auto [x0, x1] : range_spans(t.measurer, shown, caret, caret + es.preedit.size(), field.display))
            dl.line(Point{X{x0 + shift}, y}, Point{X{x1 + shift}, y}, t.text, 1.f);
    }

    if (vs.focused) {
        size_t shown_caret = caret + (es.preedit.empty() ? 0 : es.preedit_caret);
        X cursor_x{caret_x(t.measurer, shown, shown_caret, field.display) + shift};
        dl.filled_rect(delegate_detail::make_rect(cursor_x, Y{tf_padding_y.raw()}, tf_cursor_w,
                                 tf_widget_h - tf_padding_y * 2.f),
                       t.cursor);
    }

    dl.clip_pop();
}

std::optional<TextEdit> common_edit_key(const TextEdit& e, const KeyPress& kp) {
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

std::optional<std::size_t> visual_arrow(std::string_view line, std::size_t at, const TextEdit& e,
                                        const KeyPress& kp, const TextMeasurer* m, float size,
                                        FontRole role, TextDirection dir) {
    if (kp.key != keys::left && kp.key != keys::right) return std::nullopt;
    if (mods::shortcut(kp.mods) || (text_edit::has_selection(e) && !(kp.mods & mods::shift))) return std::nullopt;
    if (is_plain_ltr(bidi_runs(line, dir))) return std::nullopt;
    return visual_step(m, line, at, size, kp.key == keys::right ? +1 : -1, role, dir).value_or(at);
}

std::optional<TextEdit> clipboard_key(const TextEdit& e, const KeyPress& kp,
                                      const Clipboard* cb, bool allow_copy) {
    if (!mods::shortcut(kp.mods) || (kp.key != keys::c && kp.key != keys::x)) return std::nullopt;
    if (!cb || !allow_copy || !text_edit::has_selection(e)) return e;
    cb->set_text(std::string(text_edit::selected(e)));
    return kp.key == keys::x ? text_edit::erase_back(e) : e;
}

std::string single_line(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;   // CRLF -> one space
        out += (s[i] == '\r' || s[i] == '\n') ? ' ' : s[i];
    }
    return out;
}

std::string normalized_lines(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') { if (i + 1 >= s.size() || s[i + 1] != '\n') out += '\n'; }
        else out += s[i];
    }
    return out;
}

std::optional<std::optional<TextEdit>> history_key(EditHistory& h, const TextEdit& e, const KeyPress& kp) {
    if (!mods::shortcut(kp.mods)) return std::nullopt;
    bool shift = (kp.mods & mods::shift) != 0;
    if (kp.key == keys::z) return shift ? h.redo(e) : h.undo(e);
    if (kp.key == keys::y) return h.redo(e);
    return std::nullopt;
}

std::string drawn_line(std::string_view text, TextDirection dir) {
    if (text.empty() || paragraph_direction(text) == dir) return std::string(text);
    return (dir == TextDirection::Rtl ? "\xE2\x80\x8F" : "\xE2\x80\x8E") + std::string(text);
}

std::vector<LinePlace> place_lines(std::string_view value, std::span<const LineSpan> lines, Width area_w,
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

LineCol cursor_to_line_col(std::string_view text, size_t cursor, std::span<const LineSpan> lines,
                           bool upstream) {
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

size_t line_col_to_cursor(std::string_view text, size_t line, size_t col,
                          std::span<const LineSpan> lines) {
    if (line >= lines.size()) line = lines.size() - 1;
    return lines[line].start + core::utf8::offset(line_text(text, lines[line]), col);
}

void publish_scroll(WidgetNode& node, TextAreaEditState& es, Height content_h, Width text_area_w,
                    Height text_area_h) {
    es.viewport_h = text_area_h;
    es.content_h = content_h;
    es.scroll_y = DY{std::clamp(es.scroll_y.raw(), 0.f, std::max(0.f, content_h.raw() - text_area_h.raw()))};
    Rect viewport{Point{X{ta_padding_x.raw()}, Y{ta_padding_y.raw()}}, Size{text_area_w, text_area_h}};
    if (content_h.raw() > text_area_h.raw()) node.inner_scroll = WidgetNode::InnerScroll{viewport, content_h, es.scroll_y};
    else node.inner_scroll.reset();
}
void draw_text_area(DrawList& dl, const TextAreaDraw& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& es = node.get_or_create<TextAreaEditState>();
    const TextMeasurer* m = node.theme ? node.theme->measurer : nullptr;
    const FontRole role = field.font;
    Width w = delegate_detail::widget_w(node);
    Width text_area_w = w - ta_padding_x * 2.f;
    Height text_area_h{static_cast<float>(field.rows) * ta_line_h(m, role).raw()};
    Height widget_h = ta_padding_y * 2.f + text_area_h;

    auto& t = *node.theme;
    // Read-only text reads as plain text: its box is clear (it still sizes the widget).
    auto bg = field.read_only ? Color::rgba(0, 0, 0, 0)
            : vs.focused ? t.surface_active
            : vs.hovered ? t.surface_hover
            : t.surface;
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, widget_h), bg);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, widget_h, t);

    dl.clip_push(delegate_detail::make_point(X{ta_padding_x.raw()}, Y{ta_padding_y.raw()}), Size{text_area_w, text_area_h});

    auto raw = std::string_view(field.value.data(), field.value.size());
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
        for (const auto& h : field.highlights)
            for_each_span(std::min(h.begin, raw.size()), std::min(h.end, raw.size()), [&](float x0, float x1, Y y) {
                dl.filled_rect(delegate_detail::make_rect(X{x0}, y, Width{x1 - x0}, ta_line_h(m, role)), t.text_match);
            });
    if (!es.preedit.empty()) {   // underline the composition
        for_each_span(caret, caret + es.preedit.size(), [&](float x0, float x1, Y y) {
            Y under = y + DY{ta_line_h(m, role).raw() - 2.f};
            dl.line(Point{X{x0}, under}, Point{X{x1}, under}, t.text, 1.f);
        });
    }

    if (field.value.empty() && !vs.focused) {
        dl.text(std::string(field.placeholder), delegate_detail::make_point(X{0}, Y{2}), ta_font_size,
                t.text_placeholder, 0.f, TextAnchor::TopLeft, role);
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
} // namespace text_detail

namespace combo_detail {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::vector<std::size_t> matches(const ComboBox& c) {
    std::vector<std::size_t> out;
    auto typed = lower(c.value);
    for (std::size_t i = 0; i < c.options.size() && out.size() < max_rows; ++i)
        if (lower(c.options[i]).find(typed) != std::string::npos) out.push_back(i);
    return out;
}

std::optional<std::size_t> row_at(Point p, std::size_t rows, const WidgetNode& node) {
    float top = Widget<TextField<>>::widget_h.raw();
    if (p.y.raw() < top || p.x.raw() < 0 || p.x.raw() > delegate_detail::widget_w(node).raw()) return std::nullopt;
    auto i = static_cast<std::size_t>((p.y.raw() - top) / row_h);
    return i < rows ? std::optional{i} : std::nullopt;
}

void take(Field<ComboBox>& field, TextEditState& es, std::size_t option) {
    auto c = field.get();
    c.value = c.options[option];
    es.cursor = c.value.size();
    es.anchor.reset();
    es.completion_closed = true;
    field.set(c);
}
} // namespace combo_detail

void Widget<ComboBox>::record(DrawList& dl, const Field<ComboBox>& field, WidgetNode& node) {
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

void Widget<ComboBox>::handle_input(Field<ComboBox>& field, const InputEvent& ev, WidgetNode& node) {
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

} // namespace prism::ui