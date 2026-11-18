#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/text_metrics.hpp>

#include <cctype>
#include <string>
#include <string_view>

// A container's look around its content: a toolbar, a status bar, a titled group
// (vb.toolbar, vb.status_bar, vb.group). The look is a name, not colours: the colours come
// from the theme in use when it is drawn, so a theme switch repaints it.
namespace prism::ui {

// Card: a raised surface on its own background (a floating panel over a dock).
// NodeCard and NodeHeader: a node board's card and the band on top of it (src/app/node_board.cpp);
// the header's colour comes from the family's name through the theme, so the look stays a name.
enum class FrameLook { Toolbar, StatusBar, Group, Card, NodeCard, NodeHeader };

struct Framing {
    FrameLook look = FrameLook::Group;
    std::string title{};   // a group's, drawn above its content
    std::string family{};  // a node header's family name, for its colour
    bool operator==(const Framing&) const = default;
};

// Space between the container's edge and its content, per side.
struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
};

// The corner radius a node board's card is drawn with; its border and its ring follow it.
inline constexpr float node_card_radius = 10.f;

namespace frame_detail {
constexpr float title_h = 22.f;
constexpr float title_size = 13.f;
constexpr float node_header_mix = 0.2f;    // the family's weight over the surface, in the header
constexpr float node_label_mix = 0.6f;     // and in the family name
constexpr float node_header_left = 20.f;   // room for the family's dot, before the title
constexpr float node_dot_x = 12.f;
constexpr float node_dot_radius = 4.f;
constexpr float node_label_size = 10.f;
constexpr float node_label_pad = 10.f;     // from the header's right edge to the name
constexpr float node_label_gap = 8.f;      // between the title and the name
}

// The header's family name, upper-cased as it is drawn.
inline std::string node_badge_name(const Framing& f) {
    std::string name{f.family};
    for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

// How much room the header's family name takes: the name itself plus the gap from the title.
inline float node_badge_width(const Framing& f, const TextMeasurer* measurer) {
    if (f.family.empty()) return 0.f;
    return text_width(measurer, node_badge_name(f), frame_detail::node_label_size)
         + frame_detail::node_label_gap;
}

inline Insets frame_insets(const Framing& f) {
    switch (f.look) {
    case FrameLook::Toolbar: return {4, 4, 4, 4};
    case FrameLook::StatusBar: return {8, 2, 8, 2};
    case FrameLook::Group: return {9, f.title.empty() ? 9.f : frame_detail::title_h + 6.f, 9, 9};
    case FrameLook::Card: return {1, 1, 1, 1};
    case FrameLook::NodeCard: return {1, 1, 1, 1};
    case FrameLook::NodeHeader: return {frame_detail::node_header_left, 0, frame_detail::node_label_pad, 0};
    }
    return {};
}

// Room a look needs beyond its insets: a node header draws its family's name over the card's right
// end, so the name widens the card without narrowing the content -- the grip takes a press there.
inline float frame_reserved_width(const Framing& f, const Theme* t = nullptr) {
    if (f.look != FrameLook::NodeHeader) return 0.f;
    return node_badge_width(f, t ? t->measurer : nullptr);
}

// What the frame draws behind its content, in the container's own coordinates.
inline render::DrawList frame_draws(const Framing& f, Size size, const Theme& t) {
    using namespace render;
    DrawList dl;
    float w = size.w.raw(), h = size.h.raw();
    auto line = [&](float y) { dl.line(Point{X{0}, Y{y}}, Point{X{w}, Y{y}}, t.chrome_border, 1.f); };
    switch (f.look) {
    case FrameLook::Toolbar:
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, size}, t.chrome_bg);
        line(h - 0.5f);
        break;
    case FrameLook::StatusBar:
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, size}, t.chrome_bg);
        line(0.5f);
        break;
    case FrameLook::Group: {
        float top = f.title.empty() ? 0.f : frame_detail::title_h;
        if (!f.title.empty()) dl.text(f.title, Point{X{2}, Y{3}}, frame_detail::title_size, t.text_muted);
        dl.rect_outline(Rect{Point{X{0.5f}, Y{top + 0.5f}}, Size{Width{w - 1}, Height{h - top - 1}}}, t.popup_border, 1.f);
        break;
    }
    case FrameLook::Card:
        dl.filled_rect(Rect{Point{X{2}, Y{3}}, size}, Color::rgba(0, 0, 0, 40));   // a soft shadow
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, size}, t.popup_bg);
        dl.rect_outline(Rect{Point{X{0.5f}, Y{0.5f}}, Size{Width{w - 1}, Height{h - 1}}}, t.popup_border, 1.f);
        break;
    case FrameLook::NodeCard:
        dl.rounded_rect(Rect{Point{X{0}, Y{0}}, size}, t.node_surface, node_card_radius);
        dl.rounded_rect(Rect{Point{X{0.5f}, Y{0.5f}}, Size{Width{w - 1}, Height{h - 1}}}, t.node_border,
                        node_card_radius, 1.f);
        break;
    case FrameLook::NodeHeader: {
        const Color family = node_family_color(t, f.family);
        const Color band = mix(t.node_surface, family, frame_detail::node_header_mix);
        dl.rounded_rect(Rect{Point{X{0}, Y{0}}, size}, band, node_card_radius);
        // Only the header's top corners are the card's: square the lower two.
        dl.filled_rect(Rect{Point{X{0}, Y{h / 2.f}}, Size{Width{w}, Height{h / 2.f}}}, band);
        dl.circle(Point{X{frame_detail::node_dot_x}, Y{h / 2.f}}, frame_detail::node_dot_radius, family);
        if (!f.family.empty()) {
            const std::string name = node_badge_name(f);
            const float width = text_width(t.measurer, name, frame_detail::node_label_size);
            const float x = w - frame_detail::node_label_pad - width;
            const float y = (h - frame_detail::node_label_size) / 2.f;
            dl.text(name, Point{X{x}, Y{y}}, frame_detail::node_label_size,
                    mix(t.text, family, frame_detail::node_label_mix));
        }
        break;
    }
    }
    return dl;
}

} // namespace prism::ui
