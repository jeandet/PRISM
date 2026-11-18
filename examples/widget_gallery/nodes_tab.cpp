#include "nodes_tab.hpp"

#include <algorithm>

namespace gallery {

void SineNode::view(app::ViewBuilder& vb) {
    vb.widget(cycles);
    vb.widget(amplitude);
    vb.port(out);
}

void NoiseNode::view(app::ViewBuilder& vb) {
    vb.widget(level);
    vb.port(out);
}

void MixerNode::view(app::ViewBuilder& vb) {
    vb.port(a);
    vb.port(b);
    vb.widget(gain_a);
    vb.widget(gain_b);
    vb.port(out);
}

void LowPassNode::view(app::ViewBuilder& vb) {
    vb.port(in);
    vb.widget(cutoff);
    vb.port(out);
}

void ScopeNode::view(app::ViewBuilder& vb) {
    vb.port(in);
    vb.canvas(*this).depends_on(in.field).min_size(core::Width{200}).min_size(core::Height{110});
}

void ScopeNode::canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n) {
    const auto& theme = *n.theme;
    dl.filled_rect(r, theme.code_bg);
    const float mid = r.origin.y.raw() + r.extent.h.raw() / 2;
    const float half = r.extent.h.raw() / 2 - 4;
    dl.line({r.origin.x, core::Y{mid}}, {core::X{r.origin.x.raw() + r.extent.w.raw()}, core::Y{mid}}, theme.divider);
    const auto& w = in.get();
    if (w.size() < 2) return;
    std::vector<core::Point> pts;
    pts.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        const float x = r.origin.x.raw() + r.extent.w.raw() * static_cast<float>(i) / static_cast<float>(w.size() - 1);
        const float y = mid - half * static_cast<float>(std::clamp(w[i] / 2.0, -1.0, 1.0));
        pts.push_back({core::X{x}, core::Y{y}});
    }
    dl.polyline(std::move(pts), theme.accent, 1.5f);
}

NodesTab::NodesTab() {
    using core::X, core::Y;
    auto s = graph.add(sine, {.title = "Sine", .family = "source", .at = {X{16}, Y{16}}});
    auto z = graph.add(noise, {.title = "Noise", .family = "source", .at = {X{16}, Y{210}}});
    auto m = graph.add(mixer, {.title = "Mixer", .family = "math", .at = {X{270}, Y{70}}});
    auto f = graph.add(filter, {.title = "Low-pass", .family = "filter", .at = {X{520}, Y{215}}});
    auto r = graph.add(raw, {.title = "Scope", .family = "display", .at = {X{520}, Y{16}}});
    auto o = graph.add(smooth, {.title = "Scope", .family = "display", .at = {X{770}, Y{215}}});
    static_cast<void>(graph.connect({s, 0}, {m, 0}));
    static_cast<void>(graph.connect({z, 0}, {m, 1}));
    static_cast<void>(graph.connect({m, 2}, {r, 0}));
    static_cast<void>(graph.connect({m, 2}, {f, 0}));
    static_cast<void>(graph.connect({f, 1}, {o, 0}));
    wire.on(fit, [this](const ui::Button&) { graph.request_fit(); });
    wire.on(actual_size, [this](const ui::Button&) { graph.set_view({graph.view().pan, 1.f}); });
}

void NodesTab::view(app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(intro);
    vb.hstack([&] {
        vb.widget(fit);
        vb.widget(actual_size);
        vb.widget(zoom_label);
    });
    vb.node_board(graph);
}

} // namespace gallery
