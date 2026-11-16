// Knob: a rotary control in one header — a value type, its geometry as free functions, and one
// Widget<> specialization. Nothing to register: a Field<Knob> is a knob.
#pragma once

#include <prism/ui/delegate.hpp>
#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace gallery {

using namespace prism;

struct Knob {
    double value{};
    double min{0.0};
    double max{1.0};
    double step{};         // 0 = continuous
    std::string label{};   // under the dial, with the value under that
    std::string unit{};
    bool operator==(const Knob&) const = default;
};

// Where a value sits, and where the hand is: plain numbers, plain functions, no widget.
namespace knob_detail {

inline constexpr float from_deg{135.f};   // where min sits, in screen angles
inline constexpr float sweep_deg{270.f};  // ... and the turn from min to max
inline constexpr float dead_centre{4.f};  // nearer the centre than this, a pointer has no angle

inline double fraction(const Knob& k) {
    return k.max == k.min ? 0.0 : std::clamp((k.value - k.min) / (k.max - k.min), 0.0, 1.0);
}
inline double step_size(const Knob& k) { return k.step > 0.0 ? k.step : (k.max - k.min) / 100.0; }

inline double snapped(const Knob& k, double raw) {
    const auto [lo, hi] = std::minmax(k.min, k.max);
    const double step = step_size(k);
    return std::clamp(lo + std::round((raw - lo) / step) * step, lo, hi);
}

// The hand points at the pointer, wherever it is: the knob turns the way the hand moves. In the
// gap at the bottom it holds the nearer end, so a drag past max never wraps round to min.
inline double turned(const Knob& k, core::DX dx, core::DY dy) {
    if (std::hypot(dx.raw(), dy.raw()) < dead_centre) return k.value;
    const double deg = std::atan2(dy.raw(), dx.raw()) * 180.0 / std::numbers::pi;
    const double past_min = std::fmod(deg - from_deg + 720.0, 360.0);
    const double f = past_min <= sweep_deg ? past_min / sweep_deg : fraction(k) >= 0.5 ? 1.0 : 0.0;
    return snapped(k, k.min + f * (k.max - k.min));
}
inline double nudged(const Knob& k, int notches) { return snapped(k, k.value + notches * step_size(k)); }

inline float angle_at(double f) {
    return (from_deg + sweep_deg * static_cast<float>(f)) * std::numbers::pi_v<float> / 180.f;
}
inline core::Point point_at(core::Point centre, float radius, float angle) {
    return {centre.x + core::DX{radius * std::cos(angle)}, centre.y + core::DY{radius * std::sin(angle)}};
}
inline std::vector<core::Point> arc(core::Point centre, float radius, float from, float to) {
    constexpr int segments{48};
    std::vector<core::Point> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i)
        points.push_back(point_at(centre, radius, std::lerp(from, to, static_cast<float>(i) / segments)));
    return points;
}

} // namespace knob_detail
} // namespace gallery

namespace prism::ui {

template <>
struct Widget<gallery::Knob> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::Slider;
    static constexpr Width size{72.f};    // eight across a mixer, four across a rack panel
    static constexpr Height extent{76.f};
    static constexpr float dial_radius{22.f}, body_radius{16.f};
    static constexpr float track_thickness{4.f}, caption_size{11.f};   // the value arc is 1 px thicker
    static constexpr Y dial_y{26.f}, label_y{51.f}, value_y{64.f};

    static void describe(const Field<gallery::Knob>& f, AccessInfo& a) {
        const auto& k = f.get();
        a.name = k.label;
        a.value = delegate_detail::slider_value_text(k.value, k.step, k.unit);
        a.numeric_value = k.value;
        a.min_value = k.min;
        a.max_value = k.max;
        if (k.step > 0.0) a.step = k.step;
    }

    static void record(DrawList& dl, const Field<gallery::Knob>& f, WidgetNode& node) {
        namespace kd = gallery::knob_detail;
        const auto& vs = node_vs(node);
        const auto& t = node_theme(node);
        const auto& k = f.get();
        set_node_cursor(node, CursorShape::ResizeNS);

        const Width w{std::max(size.raw(), node_allocated(node).w.raw())};
        const X middle{w.raw() / 2.f};
        const Point centre = delegate_detail::make_point(middle, dial_y);
        const float from = kd::angle_at(0.0);
        const float to = kd::angle_at(kd::fraction(k));
        const Color accent = vs.pressed ? t.accent_active : vs.hovered ? t.accent_hover : t.accent;

        // Transparent: layout sizes a leaf by what it draws, and a knob's size is fixed.
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, w, extent),
                       Color::rgba(0, 0, 0, 0));
        // A body one shade off the panel, with an edge, or the dial reads as a floating arc.
        dl.circle(centre, body_radius, t.surface_active);
        dl.circle(centre, body_radius, t.border, 1.f);
        // track_hover, not track: the plain track shade disappears against a light surface.
        dl.polyline(kd::arc(centre, dial_radius, from, kd::angle_at(1.0)), t.track_hover,
                    track_thickness);
        dl.polyline(kd::arc(centre, dial_radius, from, to), accent, track_thickness + 1.f);
        dl.line(kd::point_at(centre, body_radius * 0.4f, to),
                kd::point_at(centre, body_radius - 1.f, to), accent, 2.f);
        dl.text(k.label, delegate_detail::make_point(middle, label_y), caption_size, t.text_muted,
                0.f, TextAnchor::Center);
        dl.text(delegate_detail::slider_value_text(k.value, k.step, k.unit),
                delegate_detail::make_point(middle, value_y), caption_size, t.text, 0.f,
                TextAnchor::Center);
        if (vs.focused) delegate_detail::draw_focus_ring(dl, w, extent, t);
    }

    // A press only grabs the knob, so a click to focus it never moves it; moves while pressed turn it.
    static void handle_input(Field<gallery::Knob>& f, const InputEvent& ev, WidgetNode& node) {
        if (turn(f, ev, node)) node.dirty = true;
    }

private:
    static bool turn(Field<gallery::Knob>& f, const InputEvent& ev, const WidgetNode& node) {
        if (const auto* mm = std::get_if<MouseMove>(&ev); mm && node_vs(node).pressed) {
            const X middle{std::max(size.raw(), node_allocated(node).w.raw()) / 2.f};
            return assign(f, gallery::knob_detail::turned(f.get(), mm->position.x - middle,
                                                          mm->position.y - dial_y));
        }
        if (const auto* kp = std::get_if<KeyPress>(&ev)) return nudge(f, *kp);
        return false;
    }
    static bool nudge(Field<gallery::Knob>& f, const KeyPress& kp) {
        const int notches = kp.key == keys::up || kp.key == keys::right   ? 1
                          : kp.key == keys::down || kp.key == keys::left ? -1
                                                                        : 0;
        if (notches == 0) return false;
        return assign(f, gallery::knob_detail::nudged(f.get(), notches));
    }
    static bool assign(Field<gallery::Knob>& f, double turned_to) {
        if (turned_to == value(f)) return false;
        set_value(f, turned_to);
        return true;
    }
};

} // namespace prism::ui
