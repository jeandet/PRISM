#pragma once

// Small form widgets: radio group, progress bar, spin box.

#include <prism/ui/delegate.hpp>
#include <prism/ui/widget_node.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace prism::ui {

// ---- Radio group ----

// Pick one of a list of strings. For an enum field, use Radio<E> instead.
struct RadioGroup {
    std::size_t selected = 0;
    std::vector<std::string> options{};
    bool operator==(const RadioGroup&) const = default;
};

// An enum field shown as radio buttons; Dropdown<E> (or a bare enum) shows it as a dropdown.
template <ScopedEnum E>
struct Radio {
    E value{};
    bool operator==(const Radio&) const = default;
};

namespace radio_detail {

constexpr float pad = 4.f;
constexpr float row_h = 26.f;
constexpr float dot_r = 8.f;
constexpr float label_x = 2 * dot_r + 16.f;
constexpr float font_size = 14.f;

inline float height(std::size_t n) { return 2 * pad + row_h * static_cast<float>(n); }

inline float width(const std::vector<std::string>& labels, const WidgetNode& n) {
    float w = 0.f;
    for (const auto& l : labels) w = std::max(w, text_width(node_theme(n).measurer, l, font_size));
    return label_x + w + 8.f;
}

inline void draw(DrawList& dl, const std::vector<std::string>& labels, std::size_t selected, WidgetNode& node) {
    const auto& t = node_theme(node);
    auto& vs = node_vs(node);
    Height h{height(labels.size())};
    delegate_detail::fill_widget_bg(dl, node, h);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        float y = pad + row_h * static_cast<float>(i);
        Point centre{X{8.f + dot_r}, Y{y + row_h / 2}};
        dl.circle(centre, dot_r, i == selected ? t.accent : t.border, 2.f);
        if (i == selected) dl.circle(centre, dot_r - 4.f, t.accent);
        dl.text(labels[i], Point{X{label_x}, Y{y + 5.f}}, font_size, t.text);
    }
    if (vs.focused) delegate_detail::draw_focus_ring(dl, delegate_detail::widget_w(node), h, t);
}

// The index an event selects, if it changes the selection.
inline std::optional<std::size_t> pick(const InputEvent& ev, std::size_t selected, std::size_t n) {
    if (n == 0) return std::nullopt;
    selected = std::min(selected, n - 1);   // the options may have shrunk under it
    std::optional<std::size_t> to;
    if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
        float row = (mb->position.y.raw() - pad) / row_h;
        if (row >= 0.f && row < static_cast<float>(n)) to = static_cast<std::size_t>(row);
    } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
        if ((kp->key == keys::down || kp->key == keys::right) && selected + 1 < n) to = selected + 1;
        else if ((kp->key == keys::up || kp->key == keys::left) && selected > 0) to = selected - 1;
    }
    return to != selected ? to : std::nullopt;
}

template <ScopedEnum E>
std::vector<std::string> enum_labels() {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < enum_count<E>(); ++i) out.push_back(enum_label<E>(i));
    return out;
}

} // namespace radio_detail

template <>
struct Widget<RadioGroup> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;   // one tab stop; arrows choose
    static constexpr Role access_role = Role::RadioGroup;
    static void describe(const Field<RadioGroup>& f, AccessInfo& a) {
        const auto& g = f.get();
        if (g.selected < g.options.size()) a.value = g.options[g.selected];
    }
    static float intrinsic_width(const Field<RadioGroup>& f, const WidgetNode& n) {
        return radio_detail::width(f.get().options, n);
    }
    static void record(DrawList& dl, const Field<RadioGroup>& f, WidgetNode& node) {
        radio_detail::draw(dl, f.get().options, f.get().selected, node);
    }
    static void handle_input(Field<RadioGroup>& f, const InputEvent& ev, WidgetNode&) {
        if (auto to = radio_detail::pick(ev, f.get().selected, f.get().options.size())) {
            auto g = f.get();
            g.selected = *to;
            f.set(g);
        }
    }
};

template <ScopedEnum E>
struct Widget<Radio<E>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::RadioGroup;
    static void describe(const Field<Radio<E>>& f, AccessInfo& a) { a.value = enum_label<E>(enum_index(f.get().value)); }
    static float intrinsic_width(const Field<Radio<E>>&, const WidgetNode& n) {
        return radio_detail::width(radio_detail::enum_labels<E>(), n);
    }
    static void record(DrawList& dl, const Field<Radio<E>>& f, WidgetNode& node) {
        radio_detail::draw(dl, radio_detail::enum_labels<E>(), enum_index(f.get().value), node);
    }
    static void handle_input(Field<Radio<E>>& f, const InputEvent& ev, WidgetNode&) {
        if (auto to = radio_detail::pick(ev, enum_index(f.get().value), enum_count<E>()))
            f.set(Radio<E>{enum_from_index<E>(*to)});
    }
};

// ---- Progress bar ----

// Display only. `busy`: the work's length is unknown -- a segment slides back and forth
// instead of the value (Qt's indeterminate progress).
struct ProgressBar {
    double value = 0.0;
    double min = 0.0;
    double max = 1.0;
    std::string label{};   // drawn over the bar when not empty
    bool busy = false;
    bool operator==(const ProgressBar&) const = default;
};

namespace progress_detail {
constexpr float bar_row_h = 16.f;
constexpr float label_h = 18.f;   // the label sits above the bar
constexpr float track_h = 8.f;
constexpr float label_size = 12.f;

inline float fraction(const ProgressBar& p) {
    if (!(p.max > p.min)) return 0.f;
    return static_cast<float>(std::clamp((p.value - p.min) / (p.max - p.min), 0.0, 1.0));
}

constexpr double busy_period_s = 1.4;   // there and back
constexpr auto busy_frame = std::chrono::milliseconds(33);

// Where the busy segment is (x, width) on a track `w` wide, `seconds` into its animation.
inline std::pair<float, float> busy_segment(float w, double seconds) {
    float seg = 0.3f * w;
    double phase = std::fmod(seconds, busy_period_s) / busy_period_s;   // 0..1
    double there_and_back = phase < 0.5 ? 2 * phase : 2 - 2 * phase;     // 0..1..0
    return {static_cast<float>(there_and_back) * (w - seg), seg};
}
} // namespace progress_detail

template <>
struct Widget<ProgressBar> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Role access_role = Role::ProgressIndicator;
    static constexpr bool expand = true;
    static constexpr ExpandAxis expand_axis = ExpandAxis::Horizontal;
    static void describe(const Field<ProgressBar>& f, AccessInfo& a) {
        const auto& p = f.get();
        if (p.busy) {   // the progress is unknown: no number to report
            a.value = p.label.empty() ? "busy" : p.label;
            return;
        }
        a.numeric_value = p.value;
        a.min_value = p.min;
        a.max_value = p.max;
        a.value = p.label.empty() ? fmt::format("{:.0f}%", 100.f * progress_detail::fraction(p)) : p.label;
    }
    static void record(DrawList& dl, const Field<ProgressBar>& f, WidgetNode& node) {
        using namespace progress_detail;
        const auto& t = node_theme(node);
        const auto& p = f.get();
        float w = delegate_detail::widget_w(node).raw();
        float top = p.label.empty() ? 0.f : label_h;
        // Reserves the whole row (layout sizes a leaf by what it draws): the bar is thinner.
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{w}, Height{top + bar_row_h}}}, Color::rgba(0, 0, 0, 0));
        if (!p.label.empty()) dl.text(p.label, Point{X{0}, Y{1}}, label_size, t.text);
        Rect track{Point{X{0}, Y{top + (bar_row_h - track_h) / 2}}, Size{Width{w}, Height{track_h}}};
        dl.rounded_rect(track, t.track, track_h / 2);
        node.redraw_at.reset();
        // simplify: a busy bar scrolled out of view keeps asking for frames (one cheap redraw
        // every 33 ms); skip it when the tree knows what is on screen.
        if (p.busy) {
            auto now = std::chrono::steady_clock::now();
            auto [x, seg] = busy_segment(w, std::chrono::duration<double>(now.time_since_epoch()).count());
            dl.rounded_rect(Rect{Point{X{x}, track.origin.y}, Size{Width{seg}, Height{track_h}}}, t.primary, track_h / 2);
            node.redraw_at = now + busy_frame;   // the tree's tick redraws it then
        } else if (float filled = fraction(p) * w; filled > 0.f) {
            dl.rounded_rect(Rect{track.origin, Size{Width{filled}, Height{track_h}}}, t.primary,
                            std::min(track_h, filled) / 2);
        }
    }
    static void handle_input(Field<ProgressBar>&, const InputEvent&, WidgetNode&) {}
};

// ---- Spin box ----

// A number with up/down steppers; typed digits commit on Enter. Mirrors Slider's data.
template <Numeric T>
struct SpinBox {
    T value{};
    T min = T{0};
    T max = T{100};
    T step = T{1};
    bool operator==(const SpinBox&) const = default;
};

// What is being typed, until Enter commits it or Escape (or losing focus) drops it.
struct SpinBoxEditState {
    std::optional<std::string> typed;
};

namespace spin_detail {
constexpr float widget_h = 30.f;
constexpr float stepper_w = 18.f;
constexpr float pad = 8.f;
constexpr float font_size = 14.f;

template <typename T>
std::string format(T v) {
    if constexpr (std::is_floating_point_v<T>) return fmt::format("{:.2f}", v);
    else return fmt::to_string(v);
}

// Into [min, max]; a range with max below min holds everything at min.
template <typename T>
T clamped(const SpinBox<T>& s, T v) {
    return std::clamp(v, s.min, std::max(s.min, s.max));
}

// One step (> 0) up (dir > 0) or down, within [min, max]. Not snapped to a step grid: like
// QSpinBox, a step adds `step` to whatever the value is. Integers step in their own type, so
// 64-bit values near the limits stay exact: the distance to the limit fits the unsigned type
// (the value is already inside the range), and a step that would pass it lands on it.
template <typename T>
T stepped(const SpinBox<T>& s, int dir) {
    T lo = s.min, hi = std::max(s.min, s.max), v = clamped(s, s.value);
    if constexpr (std::is_integral_v<T>) {
        using U = std::make_unsigned_t<T>;
        U room = dir > 0 ? U(U(hi) - U(v)) : U(U(v) - U(lo));
        if (room < U(s.step)) return dir > 0 ? hi : lo;
        return dir > 0 ? T(v + s.step) : T(v - s.step);
    } else {
        return clamped(s, dir > 0 ? v + s.step : v - s.step);
    }
}

template <typename T>
bool accepts(const SpinBox<T>& s, const std::string& typed, char c) {
    if (c >= '0' && c <= '9') return true;
    if (c == '-') return typed.empty() && s.min < T{0};
    if (c == '.') return std::is_floating_point_v<T> && typed.find('.') == std::string::npos
                         && !typed.empty() && typed.back() >= '0' && typed.back() <= '9';
    return false;
}

template <typename T>
std::optional<T> parse(const std::string& s) {
    T v{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

template <typename T>
void step_by(Field<SpinBox<T>>& f, int dir) {
    auto s = f.get();
    if (s.step <= T{0}) return;
    T next = stepped(s, dir);
    if (next != s.value) {
        s.value = next;
        f.set(s);
    }
}

template <typename T>
void commit(Field<SpinBox<T>>& f, SpinBoxEditState& es) {
    if (auto v = es.typed ? parse<T>(*es.typed) : std::nullopt) {
        auto s = f.get();
        s.value = clamped(s, *v);
        f.set(s);
    }
    es.typed.reset();
}

} // namespace spin_detail

template <Numeric T>
struct Widget<SpinBox<T>> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static constexpr Role access_role = Role::SpinButton;
    static void describe(const Field<SpinBox<T>>& f, AccessInfo& a) {
        const auto& s = f.get();
        a.value = spin_detail::format(s.value);
        a.numeric_value = static_cast<double>(s.value);
        a.min_value = static_cast<double>(s.min);
        a.max_value = static_cast<double>(s.max);
        if (s.step > T{0}) a.step = static_cast<double>(s.step);
    }
    static float intrinsic_width(const Field<SpinBox<T>>& f, const WidgetNode& n) {
        using namespace spin_detail;
        const auto* m = node_theme(n).measurer;
        float digits = std::max(text_width(m, format(f.get().min), font_size), text_width(m, format(f.get().max), font_size));
        return 2 * pad + std::max(digits, 32.f) + stepper_w;
    }

    static void record(DrawList& dl, const Field<SpinBox<T>>& f, WidgetNode& node) {
        using namespace spin_detail;
        const auto& t = node_theme(node);
        auto& vs = node_vs(node);
        auto& es = node.get_or_create<SpinBoxEditState>();
        if (!vs.focused) es.typed.reset();   // simplify: losing focus drops what was typed, like Escape
        float w = delegate_detail::widget_w(node).raw();
        delegate_detail::fill_widget_bg(dl, node, Height{widget_h});
        dl.text(es.typed ? *es.typed : format(f.get().value), Point{X{pad}, Y{7}}, font_size,
                es.typed ? t.accent : t.text);
        float sx = w - stepper_w;
        dl.filled_rect(Rect{Point{X{sx}, Y{0}}, Size{Width{1}, Height{widget_h}}}, t.border);
        float cx = sx + stepper_w / 2, a = 4.f;
        dl.filled_polygon({Point{X{cx - a}, Y{11}}, Point{X{cx + a}, Y{11}}, Point{X{cx}, Y{6}}}, t.text);
        dl.filled_polygon({Point{X{cx - a}, Y{19}}, Point{X{cx + a}, Y{19}}, Point{X{cx}, Y{24}}}, t.text);
        if (vs.focused) delegate_detail::draw_focus_ring(dl, Width{w}, Height{widget_h}, t);
    }

    // simplify: typed text has no caret or selection -- digits append, Backspace removes the last;
    // reuse the TextField editing core if spin boxes need in-place editing.
    static void handle_input(Field<SpinBox<T>>& f, const InputEvent& ev, WidgetNode& node) {
        using namespace spin_detail;
        auto& es = node.get_or_create<SpinBoxEditState>();
        auto changed = [&] { node.dirty = true; };
        if (auto* ti = std::get_if<TextInput>(&ev)) {
            for (char c : ti->text)
                if (accepts(f.get(), es.typed.value_or(""), c)) es.typed = es.typed.value_or("") + c;
            changed();
        } else if (auto* kp = std::get_if<KeyPress>(&ev)) {
            if (es.typed) {
                if (kp->key == keys::enter) commit(f, es);
                else if (kp->key == keys::escape) es.typed.reset();
                else if (kp->key == keys::backspace && !es.typed->empty()) es.typed->pop_back();
                changed();
                return;
            }
            // Page keys never arrive: routing scrolls the focused widget's container with them.
            if (kp->key == keys::up) step_by(f, 1);
            else if (kp->key == keys::down) step_by(f, -1);
        } else if (auto* ms = std::get_if<MouseScroll>(&ev); ms && ms->dy.raw() != 0.f) {
            step_by(f, ms->dy.raw() > 0.f ? 1 : -1);
        } else if (auto* mb = std::get_if<MouseButton>(&ev); mb && mb->pressed && mb->button == buttons::left) {
            float sx = delegate_detail::widget_w(node).raw() - stepper_w;
            if (mb->position.x.raw() >= sx) step_by(f, mb->position.y.raw() < widget_h / 2 ? 1 : -1);
        }
    }
};

} // namespace prism::ui
