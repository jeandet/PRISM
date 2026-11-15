#include <prism/delegates/color_delegates.hpp>

#include <prism/ui/popup_place.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace prism::ui {

namespace color_detail {

Hsv to_hsv(Color c) {
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    float h = d == 0.f ? 0.f
            : hi == r  ? 60.f * std::fmod((g - b) / d, 6.f)
            : hi == g  ? 60.f * ((b - r) / d + 2.f)
                       : 60.f * ((r - g) / d + 4.f);
    if (h < 0.f) h += 360.f;
    return {h, hi == 0.f ? 0.f : d / hi, hi};
}

Color from_hsv(Hsv c, uint8_t alpha) {
    float chroma = c.v * c.s;
    float hp = std::fmod(c.h, 360.f) / 60.f;
    float x = chroma * (1.f - std::abs(std::fmod(hp, 2.f) - 1.f));
    float m = c.v - chroma;
    float rgb[6][3] = {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x}, {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}};
    auto& p = rgb[static_cast<int>(hp) % 6];
    auto byte = [m](float f) { return static_cast<uint8_t>(std::lround(std::clamp(f + m, 0.f, 1.f) * 255.f)); };
    return Color::rgba(byte(p[0]), byte(p[1]), byte(p[2]), alpha);
}

std::string to_hex(Color c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

std::optional<Color> parse_hex(std::string_view s) {
    if (s.starts_with('#')) s.remove_prefix(1);
    if (s.size() != 6) return std::nullopt;
    uint8_t ch[3];
    for (int i = 0; i < 3; ++i) {
        auto pair = s.substr(static_cast<size_t>(i) * 2, 2);
        auto [end, err] = std::from_chars(pair.data(), pair.data() + 2, ch[i], 16);
        if (err != std::errc{} || end != pair.data() + 2) return std::nullopt;
    }
    return Color::rgba(ch[0], ch[1], ch[2]);
}

std::shared_ptr<const Image> square_image(float hue) {
    constexpr int side = static_cast<int>(square_side);
    return render_image(side, side, [hue](float fx, float fy) { return from_hsv({hue, fx, 1.f - fy}); });
}

const std::shared_ptr<const Image>& bar_image() {
    static const auto img = render_image(1, static_cast<int>(square_side),
                                         [](float, float fy) { return from_hsv({fy * 359.999f, 1.f, 1.f}); });
    return img;
}

void sync_hsv(ColorEditState& es, Color c) {
    if (from_hsv(es.hsv, c.a) == c) return;
    Hsv fresh = to_hsv(c);
    if (fresh.s == 0.f || fresh.v == 0.f) fresh.h = es.hsv.h;
    es.hsv = fresh;
}


void draw_box(DrawList& dl, Color c, const WidgetNode& node, const Theme& t) {
    auto& vs = node_vs(node);
    Width w = delegate_detail::widget_w(node);
    dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, Height{box_h}), vs.hovered ? t.surface_hover : t.surface);
    auto chip = delegate_detail::make_rect(X{4}, Y{4}, Width{swatch}, Height{swatch});
    dl.filled_rect(chip, Color::rgba(c.r, c.g, c.b));
    dl.rect_outline(chip, t.border, 1.f);
    dl.text(to_hex(c), delegate_detail::make_point(X{swatch + 12.f}, Y{7}), 14, t.text);
    if (vs.focused) delegate_detail::draw_focus_ring(dl, w, Height{box_h}, t);
}

void draw_marker(DrawList& dl, Point at, float half) {
    Rect r{Point{at.x - DX{half}, at.y - DY{half}}, Size{Width{2 * half}, Height{2 * half}}};
    dl.rect_outline(r, Color::rgba(0, 0, 0), 1.f);
    Rect in{Point{r.origin.x + DX{1}, r.origin.y + DY{1}}, Size{Width{2 * half - 2}, Height{2 * half - 2}}};
    dl.rect_outline(in, Color::rgba(255, 255, 255), 1.f);
}

void draw_popup(DrawList& dl, ColorEditState& es, Point origin, const Theme& t) {
    Rect area{origin, Size{Width{popup_w}, Height{popup_h}}};
    dl.filled_rect(area, t.popup_bg);
    dl.rect_outline(area, t.popup_border, 1.f);
    if (es.square_hue != es.hsv.h) {
        es.square = square_image(es.hsv.h);
        es.square_hue = es.hsv.h;
    }
    Rect sq = square_rect(origin), bar = bar_rect(origin);
    dl.image(es.square, sq);
    dl.image(bar_image(), bar);
    draw_marker(dl, Point{sq.origin.x + DX{es.hsv.s * square_side}, sq.origin.y + DY{(1.f - es.hsv.v) * square_side}}, 5.f);
    Y hue_y = bar.origin.y + DY{es.hsv.h / 360.f * square_side};
    dl.rect_outline(Rect{Point{bar.origin.x - DX{2}, hue_y - DY{2}}, Size{Width{bar_w + 4}, Height{4}}}, t.text, 1.f);
    es.popup_rect = area;
}

void apply(ColorEditState& es, Field<Color>& field, WidgetNode& node) {
    field.set(from_hsv(es.hsv, field.get().a));
    node.dirty = true;
}

void drag_to(ColorEditState& es, Point at, Field<Color>& field, WidgetNode& node) {
    Point popup = es.popup_rect.origin;
    if (es.drag == ColorEditState::Drag::square) {
        Rect sq = square_rect(popup);
        es.hsv.s = fraction(at.x.raw(), sq.origin.x.raw());
        es.hsv.v = 1.f - fraction(at.y.raw(), sq.origin.y.raw());
    } else if (es.drag == ColorEditState::Drag::bar) {
        es.hsv.h = std::min(fraction(at.y.raw(), bar_rect(popup).origin.y.raw()) * 360.f, 359.999f);
    }
    apply(es, field, node);
}

void press(ColorEditState& es, Point at, Field<Color>& field, WidgetNode& node) {
    if (es.open) {
        Point popup = es.popup_rect.origin;
        es.drag = square_rect(popup).contains(at) ? ColorEditState::Drag::square
                : bar_rect(popup).contains(at)    ? ColorEditState::Drag::bar
                                                  : ColorEditState::Drag::none;
        if (es.drag != ColorEditState::Drag::none) return drag_to(es, at, field, node);
        if (es.popup_rect.contains(at)) return;   // the popup's margin
    }
    es.open = !es.open;
    node.dirty = true;
}

void key(ColorEditState& es, int32_t k, Field<Color>& field, WidgetNode& node) {
    if (!es.open) {
        if (k == keys::space || k == keys::enter) es.open = true, node.dirty = true;
        return;
    }
    auto step = [&](float& part, float by) {
        part = std::clamp(part + by, 0.f, 1.f);
        apply(es, field, node);
    };
    switch (k) {
    case keys::escape:
    case keys::enter: es.open = false, node.dirty = true; break;
    case keys::left: step(es.hsv.s, -nudge); break;
    case keys::right: step(es.hsv.s, nudge); break;
    case keys::up: step(es.hsv.v, nudge); break;
    case keys::down: step(es.hsv.v, -nudge); break;
    default: break;
    }
}

} // namespace color_detail

void Widget<Color>::record(DrawList& dl, const Field<Color>& field, WidgetNode& node) {
    auto& es = node.get_or_create<ColorEditState>();
    auto& t = node_theme(node);
    color_detail::sync_hsv(es, field.get());
    color_detail::draw_box(dl, field.get(), node, t);
    node.overlay_draws.clear();
    if (es.open)
        color_detail::draw_popup(node.overlay_draws, es,
                                 popup_origin(node, Size{Width{color_detail::popup_w}, Height{color_detail::popup_h}}, color_detail::box_h), t);
}

void Widget<Color>::handle_input(Field<Color>& field, const InputEvent& ev, WidgetNode& node) {
    auto& es = node.get_or_create<ColorEditState>();
    color_detail::sync_hsv(es, field.get());
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->button == buttons::left) {
        if (mb->pressed) color_detail::press(es, mb->position, field, node);
        else es.drag = ColorEditState::Drag::none;
    } else if (auto* mm = std::get_if<MouseMove>(&ev); mm && es.drag != ColorEditState::Drag::none) {
        color_detail::drag_to(es, mm->position, field, node);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        color_detail::key(es, kp->key, field, node);
    }
}

} // namespace prism::ui
