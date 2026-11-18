#include "showcase_common.hpp"

#include <prism/nodes/graph.hpp>

#include <cmath>
#include <format>
#include <numbers>

using namespace prism;

struct Sine {
    core::Field<ui::Slider<>> frequency{{.value = 0.25, .min = 0.0, .max = 1.0}};
    nodes::Out<double> out{"signal", [this] { return std::sin(2 * std::numbers::pi * frequency.get().value); }};
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(app::ViewBuilder& vb) {
        vb.widget(frequency);
        vb.port(out);
    }
};

struct Gain {
    nodes::In<double> in{"in", 0.0};
    core::Field<ui::Slider<>> gain{{.value = 0.5, .min = 0.0, .max = 2.0}};
    nodes::Out<double> out{"out", [this] { return in.get() * gain.get().value; }};
    std::vector<nodes::Port*> ports() { return {&in, &out}; }
    void view(app::ViewBuilder& vb) {
        vb.port(in);
        vb.widget(gain);
        vb.port(out);
    }
};

struct Display {
    nodes::In<double> in{"value", 0.0};
    core::Field<ui::Label<>> text{{.value = "0.000", .flat = true}};
    core::Connection shown = in.on_change([this] {
        text.set({.value = std::format("{:.3f}", in.get()), .flat = true});
    });
    std::vector<nodes::Port*> ports() { return {&in}; }
    void view(app::ViewBuilder& vb) {
        vb.port(in);
        vb.widget(text);
    }
};

struct Patch {
    Sine sine;
    Gain gain;
    Display display;
    nodes::Graph graph;   // after the nodes: it is destroyed before them
    nodes::NodeId s = graph.add(sine, {.title = "Sine", .family = "source", .at = {core::X{20}, core::Y{50}}});
    nodes::NodeId g = graph.add(gain, {.title = "Gain", .family = "math", .at = {core::X{250}, core::Y{130}}});
    nodes::NodeId d = graph.add(display, {.title = "Display", .family = "display", .at = {core::X{470}, core::Y{60}}});

    Patch() {
        static_cast<void>(graph.connect({s, 0}, {g, 0}));
        static_cast<void>(graph.connect({g, 1}, {d, 0}));
    }

    void view(app::ViewBuilder& vb) { vb.node_board(graph); }
};

int main(int argc, char* argv[]) {
    Patch model;
    return showcase(argc, argv, model, 700, 300);
}
