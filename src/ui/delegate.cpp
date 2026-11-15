#include <prism/ui/delegate.hpp>

#include <prism/core/field.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/cursor.hpp>
#include <prism/ui/text_metrics.hpp>
#include <prism/ui/url_opener.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <string_view>

namespace prism::ui {

void Widget<Checkbox>::describe(const Field<Checkbox>& f, AccessInfo& a) {
    a.name = f.get().label;
    a.checked = f.get().checked;
}

void Widget<Checkbox>::record(DrawList& dl, const Field<Checkbox>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    auto& cb = field.get();

    delegate_detail::fill_widget_bg(dl, node, widget_h);

    Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
    draw_check_box(dl, X{8}, box_y, cb.checked, vs, t);

    if (!cb.label.empty())
        dl.text(cb.label, delegate_detail::make_point(X{32}, Y{7}), 14, t.text);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, delegate_detail::widget_w(node), widget_h, t);
}

void Widget<Checkbox>::handle_input(Field<Checkbox>& field, const InputEvent& ev, WidgetNode& node) {
    if (delegate_detail::is_activation_event(ev, node)) {
        auto cb = field.get();
        cb.checked = !cb.checked;
        field.set(cb);
    }
}

void Widget<bool>::record(DrawList& dl, const Field<bool>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    constexpr Height widget_h{30.f};
    constexpr Height box_size{16.f};

    delegate_detail::fill_widget_bg(dl, node, widget_h);

    Y box_y{(widget_h.raw() - box_size.raw()) / 2.f};
    draw_check_box(dl, X{8}, box_y, field.get(), vs, t);

    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, delegate_detail::widget_w(node), widget_h, t);
}

void Widget<bool>::handle_input(Field<bool>& field, const InputEvent& ev, WidgetNode& node) {
    if (delegate_detail::is_activation_event(ev, node))
        field.set(!field.get());
}

void delegate_detail::draw_label(DrawList& dl, const WidgetNode& node, std::string_view text, float size,
                                 bool heading, bool wrap) {
    auto& t = node_theme(node);
    Width w = widget_w(node);
    auto lines = wrap ? wrap_lines(text, Width{std::max(1.f, w.raw() - 8.f)}, t.measurer, size, FontRole::Ui)
                      : std::vector<LineSpan>{{0, text.size()}};
    float line_h = line_height(t.measurer, size);
    float one_line = heading ? 30.f : 24.f;
    Height h{one_line + line_h * static_cast<float>(std::max<std::size_t>(lines.size(), 1) - 1)};
    // A heading's box is clear: it still gives the widget its height.
    dl.filled_rect(make_rect(X{0}, Y{0}, w, h), heading ? Color::rgba(0, 0, 0, 0) : t.surface);
    for (std::size_t i = 0; i < lines.size(); ++i)
        dl.text(std::string(text.substr(lines[i].start, lines[i].length)),
                make_point(X{4}, Y{4 + line_h * static_cast<float>(i)}), size,
                heading ? t.text : t.text_muted);
}

Size Widget<Picture>::box(const Picture& p) {
    if (p.size) return *p.size;
    if (p.image) return Size{Width{static_cast<float>(p.image->width())}, Height{static_cast<float>(p.image->height())}};
    return Size{Width{0}, Height{0}};
}

Rect Widget<Picture>::fitted(const Image& img, Size area) {
    float scale = std::min(area.w.raw() / static_cast<float>(img.width()), area.h.raw() / static_cast<float>(img.height()));
    Size s{Width{static_cast<float>(img.width()) * scale}, Height{static_cast<float>(img.height()) * scale}};
    return Rect{Point{X{(area.w.raw() - s.w.raw()) / 2}, Y{(area.h.raw() - s.h.raw()) / 2}}, s};
}

void Widget<Picture>::record(DrawList& dl, const Field<Picture>& field, WidgetNode& node) {
    const auto& p = field.get();
    Size area = box(p);
    dl.filled_rect(Rect{Point{X{0}, Y{0}}, area}, Color::rgba(0, 0, 0, 0));   // the box is the widget's, image or not
    if (p.image && area.w.raw() > 0 && area.h.raw() > 0)
        dl.image(p.image, fitted(*p.image, area), icon_tint(p.image, node_theme(node).text));
}

void Widget<Link>::record(DrawList& dl, const Field<Link>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    const auto& l = field.get();
    set_node_cursor(node, CursorShape::Pointer);
    Color c = vs.pressed ? t.accent_active : vs.hovered ? t.accent_hover : t.accent;
    float w = text_width(t.measurer, l.text, font_size);
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, Width{w + 8.f}, Height{24}), Color::rgba(0, 0, 0, 0));
    dl.text(l.text, delegate_detail::make_point(X{4}, Y{4}), font_size, c);
    dl.line(Point{X{4}, Y{20}}, Point{X{4 + w}, Y{20}}, c, 1.f);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, Width{w + 8.f}, Height{24}, t);
}

void Widget<Link>::handle_input(Field<Link>& field, const InputEvent& ev, WidgetNode& node) {
    if (!delegate_detail::is_activation_event(ev, node)) return;
    auto l = field.get();
    ++l.click_count;
    if (!l.url.empty() && node_theme(node).url_opener) node_theme(node).url_opener->open(l.url);
    field.set(l);
}

void Widget<Expander>::describe(const Field<Expander>& f, AccessInfo& a) {
    a.name = f.get().title;
    a.expanded = f.get().open;
}

void Widget<Expander>::record(DrawList& dl, const Field<Expander>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    Width w = delegate_detail::widget_w(node);
    const auto& e = field.get();
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, header_h),
                   vs.pressed ? t.surface_active : vs.hovered ? t.surface_hover : Color::rgba(0, 0, 0, 0));
    dl.text(e.open ? "\xe2\x96\xbe" : "\xe2\x96\xb8", delegate_detail::make_point(X{6}, Y{5}), font_size, t.text_muted);   // ▾ ▸
    dl.text(e.title, delegate_detail::make_point(X{22}, Y{5}), font_size, t.text);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, header_h, t);
}

void Widget<Expander>::handle_input(Field<Expander>& field, const InputEvent& ev, WidgetNode& node) {
    if (!delegate_detail::is_activation_event(ev, node)) return;
    auto e = field.get();
    e.open = !e.open;
    field.set(e);
}

namespace delegate_detail {

static void draw_slider_caption(DrawList& dl, const SliderLook& look, const Theme& t) {
    constexpr float inset = 4.f;   // as a label's text
    dl.text(look.label, make_point(X{inset}, Y{2}), slider_caption_size, t.text_muted);
    float vw = text_width(t.measurer, look.value_text, slider_caption_size);
    dl.text(look.value_text, make_point(X{std::max(inset, look.track_len - inset - vw)}, Y{2}),
            slider_caption_size, t.text);
}

void draw_slider(DrawList& dl, const SliderLook& look, const WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    auto track_bg = vs.hovered ? t.track_hover : t.track;
    auto thumb_color = vs.pressed ? t.accent_active
                     : vs.hovered ? t.accent_hover
                     : t.accent;

    if (look.vertical) {
        X track_x{(slider_widget_extent - slider_track_thick) / 2.f};
        dl.filled_rect(make_rect(track_x, Y{0}, Width{slider_track_thick}, Height{look.track_len}), track_bg);
        Y thumb_y{(1.f - look.ratio) * (look.track_len - slider_thumb_len)};
        dl.filled_rect(make_rect(X{0}, thumb_y, Width{slider_widget_extent}, Height{slider_thumb_len}), thumb_color);
        if (vs.focused)
            draw_focus_ring(dl, Width{slider_widget_extent}, Height{look.track_len}, t);
        return;
    }

    float top = look.label.empty() ? 0.f : slider_caption_h;
    if (top > 0.f) draw_slider_caption(dl, look, t);
    Y track_y{top + (slider_widget_extent - slider_track_thick) / 2.f};
    dl.filled_rect(make_rect(X{0}, track_y, Width{look.track_len}, Height{slider_track_thick}), track_bg);
    X thumb_x{look.ratio * (look.track_len - slider_thumb_len)};
    dl.filled_rect(make_rect(thumb_x, Y{top}, Width{slider_thumb_len}, Height{slider_widget_extent}), thumb_color);
    if (vs.focused)
        draw_focus_ring(dl, Width{look.track_len}, Height{top + slider_widget_extent}, t);
}

} // namespace delegate_detail

float Widget<Button>::intrinsic_width(const Field<Button>& f, const WidgetNode& n) {
    const auto& b = f.get();
    if (b.text.empty() && b.icon) return 32.f;   // an icon alone: square
    float icon = b.icon ? icon_size + icon_gap : 0.f;
    const auto* m = node_theme(n).measurer;
    float keys = b.shortcut.key ? shortcut_gap + text_width(m, shortcut_text(b.shortcut), 14.f) : 0.f;
    return std::max(48.f, 16.f + icon + text_width(m, b.text, 14.f) + keys);   // 8 pt each side
}

void Widget<Button>::describe(const Field<Button>& f, AccessInfo& a) {
    a.name = f.get().text;
    a.checked = f.get().checked;
}

void Widget<Button>::record(DrawList& dl, const Field<Button>& field, WidgetNode& node) {
    auto& vs = node_vs(node);
    auto& t = node_theme(node);
    Width w = delegate_detail::widget_w(node);
    const auto& b = field.get();
    bool down = vs.pressed || b.checked.value_or(false);
    Color bg = b.primary ? (down ? t.primary_active : vs.hovered ? t.primary_hover : t.primary)
                         : (down ? t.surface_active : vs.hovered ? t.surface_hover : t.surface);
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{32}), bg);
    if (b.primary) dl.rect_outline(delegate_detail::make_rect(X{0}, Y{0}, w, Height{32}), t.primary_outline, 1.0f);
    float x = 8.f;
    Color fg = b.primary ? t.text_on_primary : t.text;
    if (b.icon) {
        dl.image(b.icon, Rect{Point{X{x}, Y{(32.f - icon_size) / 2}}, Size{Width{icon_size}, Height{icon_size}}},
                 icon_tint(b.icon, fg));
        x += icon_size + icon_gap;
    }
    if (!b.text.empty()) dl.text(b.text, delegate_detail::make_point(X{x}, Y{7}), 14, fg);
    if (b.shortcut.key) {
        auto keys = shortcut_text(b.shortcut);
        float kx = w.raw() - 8.f - text_width(t.measurer, keys, 14.f);
        dl.text(keys, delegate_detail::make_point(X{kx}, Y{7}), 14, b.primary ? fg : t.text_muted);
    }
    if (vs.focused)
        delegate_detail::draw_focus_ring(dl, w, Height{32}, t);
}

bool Widget<Button>::try_shortcut(Field<Button>& field, const KeyPress& kp, WidgetNode&) {
    if (!matches(field.get().shortcut, kp)) return false;
    press(field);
    return true;
}

void Widget<Button>::press(Field<Button>& field) {
    auto btn = field.get();
    btn.click_count++;
    if (btn.checked) btn.checked = !*btn.checked;
    field.set(btn);
}

void Widget<Button>::handle_input(Field<Button>& field, const InputEvent& ev, WidgetNode& node) {
    if (delegate_detail::is_activation_event(ev, node)) press(field);
}

void Widget<Choice>::describe(const Field<Choice>& f, AccessInfo& a) {
    if (auto* s = f.get().chosen()) a.value = *s;
}

} // namespace prism::ui
