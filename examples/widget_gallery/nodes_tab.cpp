#include "nodes_tab.hpp"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <utility>

namespace gallery {

void SineNode::view(app::ViewBuilder& vb) {
    vb.widget(cycles);
    vb.widget(amplitude);
    vb.port(out);
}

void SquareNode::view(app::ViewBuilder& vb) {
    vb.widget(cycles);
    vb.widget(amplitude);
    vb.port(out);
}

void NoiseNode::view(app::ViewBuilder& vb) {
    vb.widget(level);
    vb.port(out);
}

void ConstantNode::view(app::ViewBuilder& vb) {
    vb.widget(value);
    vb.port(out);
}

void GainNode::view(app::ViewBuilder& vb) {
    vb.port(in);
    vb.port(by);
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

void NumberDisplayNode::show(double value) {
    auto l = readout.get();
    l.value = std::format("{:.3g}", value);
    readout.set(std::move(l));
}

void NumberDisplayNode::view(app::ViewBuilder& vb) {
    vb.port(in);
    vb.widget(readout);
}

NodesTab::NodesTab() {
    // The palette's kinds carry each block's title and family: the starting patch spawns them
    // through the same `add`, so a block looks the same however it arrives.
    const auto add = [this](std::string_view block, float x, float y) {
        const auto kind = std::ranges::find(kinds, block, &nodes::NodeKind::title);
        assert(kind != kinds.end());
        return kind->add(graph, core::Point{core::X{x}, core::Y{y}});
    };
    const auto sine = add("Sine", 20, 20);
    const auto square = add("Square", 20, 210);
    const auto constant = add("Constant", 300, 20);
    const auto noise = add("Noise", 300, 140);
    const auto gain = add("Gain", 300, 260);
    const auto mixer = add("Mixer", 300, 400);
    const auto number = add("Number", 610, 20);
    const auto low_pass = add("Low-pass", 610, 160);
    const auto scope = add("Scope", 900, 20);
    const auto filtered = add("Scope", 900, 220);
    const auto gained = add("Scope", 900, 420);

    const std::pair<nodes::PortRef, nodes::PortRef> wires[]{
        {{sine, 0}, {mixer, 0}},        {{noise, 0}, {mixer, 1}},
        {{mixer, 2}, {scope, 0}},       {{mixer, 2}, {low_pass, 0}},
        {{low_pass, 1}, {filtered, 0}}, {{square, 0}, {gain, 0}},
        {{constant, 0}, {gain, 1}},     {{constant, 0}, {number, 0}},
        {{gain, 2}, {gained, 0}}};
    for (const auto& [from, to] : wires)
        if (!graph.connect(from, to)) assert(false && "the starting patch wires what it may connect");

    wire.on(fit, [this](const ui::Button&) { graph.request_fit(); });
    wire.on(actual_size, [this](const ui::Button&) { graph.request_zoom(1.f); });
    graph.request_fit();   // the whole patch on show, whatever the tab's size
}

void NodesTab::view(app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(intro);
    vb.hstack([&] {
        vb.node_palette(graph, kinds);
        vb.vstack([&] {
            vb.hstack([&] {
                vb.widget(fit);
                vb.widget(actual_size);
                vb.widget(zoom_label);
            });
            vb.node_board(graph);
        });
    });
}

} // namespace gallery
