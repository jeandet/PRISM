#include "nodes_tab.hpp"

#include <algorithm>
#include <utility>

namespace gallery {

namespace {

// A patch node at (x, y) in graph units: the graph owns it, so Delete takes it off the board.
nodes::NodeOptions placed(float x, float y, std::string title, std::string family) {
    return {.title = std::move(title), .family = std::move(family),
            .at = core::Point{core::X{x}, core::Y{y}}, .removable = true};
}

} // namespace

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
    const auto sine = graph.emplace<SineNode>(placed(20, 20, "Sine", "source"));
    const auto square = graph.emplace<SquareNode>(placed(20, 210, "Square", "source"));
    const auto noise = graph.emplace<NoiseNode>(placed(300, 140, "Noise", "source"));
    const auto constant = graph.emplace<ConstantNode>(placed(300, 20, "Constant", "source"));
    const auto gain = graph.emplace<GainNode>(placed(300, 260, "Gain", "math"));
    const auto mixer = graph.emplace<MixerNode>(placed(300, 400, "Mixer", "math"));
    const auto number = graph.emplace<NumberDisplayNode>(placed(610, 20, "Number", "display"));
    const auto low_pass = graph.emplace<LowPassNode>(placed(610, 160, "Low-pass", "filter"));
    const auto scope = graph.emplace<ScopeNode>(placed(900, 20, "Scope", "display"));
    const auto filtered = graph.emplace<ScopeNode>(placed(900, 220, "Scope", "display"));
    static_cast<void>(graph.connect({sine, 0}, {mixer, 0}));
    static_cast<void>(graph.connect({noise, 0}, {mixer, 1}));
    static_cast<void>(graph.connect({mixer, 2}, {scope, 0}));
    static_cast<void>(graph.connect({mixer, 2}, {low_pass, 0}));
    static_cast<void>(graph.connect({low_pass, 1}, {filtered, 0}));
    static_cast<void>(graph.connect({square, 0}, {gain, 0}));
    static_cast<void>(graph.connect({constant, 0}, {gain, 1}));
    static_cast<void>(graph.connect({gain, 2}, {number, 0}));
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
