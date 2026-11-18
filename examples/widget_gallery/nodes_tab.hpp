// Nodes: a small signal patch on a node board. Each block is a plain struct: its controls are
// fields, its ports In/Out members, its view() places both. Wiring a port to another links the
// values, so moving a slider redraws every scope downstream. The signal math is patch.hpp.
#pragma once

#include "patch.hpp"

#include <prism/nodes/graph.hpp>
#include <prism/prism.hpp>

#include <format>
#include <string>
#include <vector>

namespace gallery {

using namespace prism;
using patch::Wave;

struct SineNode {
    core::Field<ui::Slider<>> cycles{{.value = 3, .min = 0.5, .max = 12, .step = 0.5, .label = "Cycles"}};
    core::Field<ui::Slider<>> amplitude{{.value = 0.8, .min = 0, .max = 1, .step = 0.05, .label = "Amplitude"}};
    nodes::Out<Wave> out{"wave", [this] { return patch::sine(cycles.get().value, amplitude.get().value, 0); }};
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(app::ViewBuilder& vb);
};

struct NoiseNode {
    core::Field<ui::Slider<>> level{{.value = 0.3, .min = 0, .max = 1, .step = 0.05, .label = "Level"}};
    nodes::Out<Wave> out{"wave", [this] { return patch::noise(level.get().value); }};
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(app::ViewBuilder& vb);
};

struct MixerNode {
    nodes::In<Wave> a{"a"};
    nodes::In<Wave> b{"b"};
    core::Field<ui::Slider<>> gain_a{{.value = 1, .min = 0, .max = 2, .step = 0.05, .label = "Gain a"}};
    core::Field<ui::Slider<>> gain_b{{.value = 1, .min = 0, .max = 2, .step = 0.05, .label = "Gain b"}};
    nodes::Out<Wave> out{"mix", [this] { return patch::mix(a.get(), b.get(), gain_a.get().value, gain_b.get().value); }};
    std::vector<nodes::Port*> ports() { return {&a, &b, &out}; }
    void view(app::ViewBuilder& vb);
};

struct LowPassNode {
    nodes::In<Wave> in{"in"};
    core::Field<ui::Slider<>> cutoff{{.value = 0.15, .min = 0.01, .max = 1, .step = 0.01, .label = "Cutoff"}};
    nodes::Out<Wave> out{"out", [this] { return patch::low_pass(in.get(), cutoff.get().value); }};
    std::vector<nodes::Port*> ports() { return {&in, &out}; }
    void view(app::ViewBuilder& vb);
};

// Draws what reaches its input; nothing wired, a flat line.
struct ScopeNode {
    nodes::In<Wave> in{"in"};
    std::vector<nodes::Port*> ports() { return {&in}; }
    void view(app::ViewBuilder& vb);
    void canvas(render::DrawList& dl, core::Rect r, const ui::WidgetNode& n);
};

struct NodesTab {
    core::Field<ui::Label<>> title{{.value = "Nodes", .heading = true}};
    core::Field<ui::Label<>> intro{{.value = "Drag a header to move a block, the empty board to pan. Ctrl+wheel "
                                             "zooms; a middle drag pans too. Drag from a port to another to wire "
                                             "them; drag a wire off an input to unplug it. Click a wire, then "
                                             "Delete. Move a slider: the scopes follow.",
                                    .wrap = true}};
    SineNode sine;
    NoiseNode noise;
    MixerNode mixer;
    LowPassNode filter;
    ScopeNode raw;
    ScopeNode smooth;
    nodes::Graph graph;   // after the nodes: it is destroyed before them
    core::Field<ui::Button> fit{{.text = "Fit", .primary = false}};
    core::Field<ui::Button> actual_size{{.text = "100 %", .primary = false}};
    core::Derived<std::string> zoom_label{[this] {
        return std::format("{:.0f} %", graph.view().zoom * 100.f);
    }};
    core::Wiring wire;

    NodesTab();
    void view(app::ViewBuilder& vb);
};

} // namespace gallery
