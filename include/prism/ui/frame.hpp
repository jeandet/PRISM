#pragma once

#include <prism/render/draw_list.hpp>
#include <prism/ui/context.hpp>

#include <string>

// A container's look around its content: a toolbar, a status bar, a titled group
// (vb.toolbar, vb.status_bar, vb.group). The look is a name, not colours: the colours come
// from the theme in use when it is drawn, so a theme switch repaints it.
namespace prism::ui {

// Card: a raised surface on its own background (a floating panel over a dock).
enum class FrameLook { Toolbar, StatusBar, Group, Card };

struct Framing {
    FrameLook look = FrameLook::Group;
    std::string title{};   // a group's, drawn above its content
    bool operator==(const Framing&) const = default;
};

// Space between the container's edge and its content, per side.
struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
};

namespace frame_detail {
constexpr float title_h = 22.f;
constexpr float title_size = 13.f;
}

inline Insets frame_insets(const Framing& f) {
    switch (f.look) {
    case FrameLook::Toolbar: return {4, 4, 4, 4};
    case FrameLook::StatusBar: return {8, 2, 8, 2};
    case FrameLook::Group: return {9, f.title.empty() ? 9.f : frame_detail::title_h + 6.f, 9, 9};
    case FrameLook::Card: return {1, 1, 1, 1};
    }
    return {};
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
    }
    return dl;
}

} // namespace prism::ui
