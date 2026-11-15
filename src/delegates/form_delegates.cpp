#include <prism/delegates/form_delegates.hpp>

#include <prism/ui/widget_node.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace prism::ui {

namespace radio_detail {

float width(const std::vector<std::string>& labels, const WidgetNode& n) {
    float w = 0.f;
    for (const auto& l : labels) w = std::max(w, text_width(node_theme(n).measurer, l, font_size));
    return label_x + w + 8.f;
}

void draw(DrawList& dl, const std::vector<std::string>& labels, std::size_t selected, WidgetNode& node) {
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

std::optional<std::size_t> pick(const InputEvent& ev, std::size_t selected, std::size_t n) {
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

} // namespace radio_detail

void Widget<RadioGroup>::describe(const Field<RadioGroup>& f, AccessInfo& a) {
    const auto& g = f.get();
    if (g.selected < g.options.size()) a.value = g.options[g.selected];
}

void Widget<RadioGroup>::record(DrawList& dl, const Field<RadioGroup>& f, WidgetNode& node) {
    radio_detail::draw(dl, f.get().options, f.get().selected, node);
}

void Widget<RadioGroup>::handle_input(Field<RadioGroup>& f, const InputEvent& ev, WidgetNode&) {
    if (auto to = radio_detail::pick(ev, f.get().selected, f.get().options.size())) {
        auto g = f.get();
        g.selected = *to;
        f.set(g);
    }
}

namespace progress_detail {

float fraction(const ProgressBar& p) {
    if (!(p.max > p.min)) return 0.f;
    return static_cast<float>(std::clamp((p.value - p.min) / (p.max - p.min), 0.0, 1.0));
}

std::pair<float, float> busy_segment(float w, double seconds) {
    float seg = 0.3f * w;
    double phase = std::fmod(seconds, busy_period_s) / busy_period_s;   // 0..1
    double there_and_back = phase < 0.5 ? 2 * phase : 2 - 2 * phase;     // 0..1..0
    return {static_cast<float>(there_and_back) * (w - seg), seg};
}
} // namespace progress_detail

void Widget<ProgressBar>::describe(const Field<ProgressBar>& f, AccessInfo& a) {
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

void Widget<ProgressBar>::record(DrawList& dl, const Field<ProgressBar>& f, WidgetNode& node) {
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

} // namespace prism::ui
