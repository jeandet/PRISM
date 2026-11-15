#pragma once

#include <prism/ui/delegate.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::ui {

namespace color_detail {

// https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB
// Defined in src/delegates/color_delegates.cpp.
Hsv to_hsv(Color c);

// https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_RGB
Color from_hsv(Hsv c, uint8_t alpha = 255);

std::string to_hex(Color c);

// "#RRGGBB" or "RRGGBB"; the alpha is opaque.
std::optional<Color> parse_hex(std::string_view s);

// The picker's geometry, in the widget's own coordinates: the closed box is a swatch and the hex
// code; the popup below (or above) it holds the square and, right of it, the hue bar.
constexpr float box_h = 30.f, swatch = 22.f, pad = 8.f, square_side = 160.f, bar_w = 16.f;
constexpr float popup_w = pad + square_side + pad + bar_w + pad;
constexpr float popup_h = pad + square_side + pad;
constexpr float nudge = 0.02f;   // an arrow key's step in saturation or value

inline Rect square_rect(Point popup) {
    return Rect{Point{popup.x + DX{pad}, popup.y + DY{pad}}, Size{Width{square_side}, Height{square_side}}};
}
inline Rect bar_rect(Point popup) {
    return Rect{Point{popup.x + DX{pad + square_side + pad}, popup.y + DY{pad}}, Size{Width{bar_w}, Height{square_side}}};
}

inline std::shared_ptr<const Image> render_image(int w, int h, auto&& color_at) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Color c = color_at(static_cast<float>(x) / static_cast<float>(std::max(w - 1, 1)),
                               static_cast<float>(y) / static_cast<float>(std::max(h - 1, 1)));
            auto* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
            p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = 255;
        }
    return *Image::from_rgba(w, h, std::move(px));
}

std::shared_ptr<const Image> square_image(float hue);

const std::shared_ptr<const Image>& bar_image();

// The picked color as HSV, kept in step with the field: a color changed elsewhere is taken up,
// keeping the hue the old one had when the new one has none (grey).
void sync_hsv(ColorEditState& es, Color c);

void draw_box(DrawList& dl, Color c, const WidgetNode& node, const Theme& t);

void draw_marker(DrawList& dl, Point at, float half);

void draw_popup(DrawList& dl, ColorEditState& es, Point origin, const Theme& t);

inline float fraction(float pos, float start) { return std::clamp((pos - start) / square_side, 0.f, 1.f); }

// The picked color changed: the field takes it, and the popup redraws even when the field does not
// change (a new hue for black is still black).
void apply(ColorEditState& es, Field<Color>& field, WidgetNode& node);

// Moves the picked color to the pointer, in the part the drag started in.
void drag_to(ColorEditState& es, Point at, Field<Color>& field, WidgetNode& node);

void press(ColorEditState& es, Point at, Field<Color>& field, WidgetNode& node);

void key(ColorEditState& es, int32_t k, Field<Color>& field, WidgetNode& node);

} // namespace color_detail

inline void Widget<Color>::describe(const Field<Color>& f, AccessInfo& a) { a.value = color_detail::to_hex(f.get()); }

} // namespace prism::ui
