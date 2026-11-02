#pragma once

#include <prism/core/types.hpp>
#include <prism/render/draw_list.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/ui/context.hpp>
#include <prism/ui/text_metrics.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>

// A widget's tooltip: shown after the pointer rests on the widget, hidden when it leaves, or
// by a click, key or scroll until it leaves. Pure state transitions over explicit times; the
// widget tree feeds them and the app wakes the logic thread at tooltip_wake().
namespace prism::ui {

inline constexpr std::chrono::milliseconds tooltip_delay{600};

struct TooltipState {
    using clock = std::chrono::steady_clock;
    render::WidgetId target = 0;      // the widget the pointer rests on, if it has a tooltip
    core::Point pointer{};            // where the box goes; frozen once shown
    clock::time_point due{};          // when it shows; max() once dismissed
    bool shown = false;
};

inline TooltipState tooltip_hovered(TooltipState s, render::WidgetId id, bool has_tooltip, core::Point at,
                                    TooltipState::clock::time_point now) {
    if (id != 0 && id == s.target) {
        if (!has_tooltip) return {};   // its tooltip went away under the pointer
        if (!s.shown) s.pointer = at;
        return s;
    }
    if (!has_tooltip) return {};
    return {id, at, now + tooltip_delay, false};
}

inline TooltipState tooltip_ticked(TooltipState s, TooltipState::clock::time_point now) {
    if (s.target != 0 && !s.shown && now >= s.due) s.shown = true;
    return s;
}

inline TooltipState tooltip_dismissed(TooltipState s) {
    s.shown = false;
    s.due = TooltipState::clock::time_point::max();
    return s;
}

inline std::optional<TooltipState::clock::time_point> tooltip_wake(const TooltipState& s) {
    if (s.target == 0 || s.shown || s.due == TooltipState::clock::time_point::max()) return std::nullopt;
    return s.due;
}

namespace tooltip_metrics {
constexpr float pad_x = 6.f, pad_y = 4.f;
constexpr float below = 20.f;   // clears a typical arrow cursor
constexpr float above = 4.f;
constexpr float font_size = 13.f;
}

// Below the pointer; flipped above it when it would leave the bottom of the window, then slid
// back inside horizontally (and pinned to the top if the window is shorter than the box).
inline core::Rect tooltip_rect(core::Point pointer, core::Size text, core::Size window) {
    using namespace tooltip_metrics;
    float w = text.w.raw() + 2 * pad_x, h = text.h.raw() + 2 * pad_y;
    float y = pointer.y.raw() + below;
    if (y + h > window.h.raw()) y = pointer.y.raw() - above - h;
    float x = std::clamp(pointer.x.raw(), 0.f, std::max(0.f, window.w.raw() - w));
    y = std::clamp(y, 0.f, std::max(0.f, window.h.raw() - h));
    return core::Rect{core::Point{X{x}, Y{y}}, core::Size{Width{w}, Height{h}}};
}

// simplify: one line, no wrapping; a long tooltip is as wide as its text (then clamped to the
// window). Wrap at a max width with the TextArea word breaker when long tooltips show up.
inline void draw_tooltip(render::DrawList& dl, std::string_view text, core::Point pointer,
                         core::Size window, const Theme& theme) {
    using namespace tooltip_metrics;
    core::Size size{Width{text_width(theme.measurer, text, font_size)}, Height{line_height(theme.measurer, font_size)}};
    auto r = tooltip_rect(pointer, size, window);
    dl.filled_rect(r, theme.popup_bg);
    dl.rect_outline(r, theme.popup_border, 1.f);
    dl.text(std::string(text), core::Point{r.origin.x + DX{pad_x}, r.origin.y + DY{pad_y}}, font_size, theme.text);
}

} // namespace prism::ui
