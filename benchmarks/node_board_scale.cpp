// Perf-lab benchmark: what one frame costs on a big node board (doc/design, node editor zoom plan).
// 200 nodes (a slider, two inputs, one output each) and 300 wires on a 1600x1000 headless tree.
// Each case times what model_app does per publish: build_snapshot() then clear_dirty(), averaged
// over 50 publishes, at zoom 1, 0.5 and 0.25, and at the view that fits every card (all 200 shown).
//
//   ./builddir/benchmarks/bench_node_board_scale
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/graph.hpp>

#include <fmt/format.h>

#include <chrono>
#include <memory>
#include <vector>

using namespace prism;
using app::WidgetTree;

namespace {
constexpr std::size_t node_count = 200, wire_count = 300, columns = 20, reps = 50;
constexpr float win_w = 1600.f, win_h = 1000.f;

struct Block {
    core::Field<ui::Slider<>> level{{.value = 0.5, .min = 0, .max = 1, .step = 0.05, .label = "Level"}};
    nodes::In<double> a{"a", 0.0};
    nodes::In<double> b{"b", 0.0};
    nodes::Out<double> out{"out", [this] { return level.get().value; }};
    std::vector<nodes::Port*> ports() { return {&a, &b, &out}; }
    void view(app::ViewBuilder& vb) {
        vb.port(a);
        vb.port(b);
        vb.widget(level);
        vb.port(out);
    }
};

struct Board {
    std::unique_ptr<Block[]> blocks = std::make_unique<Block[]>(node_count);
    nodes::Graph graph;   // after the blocks: destroyed before them
    std::vector<nodes::NodeId> ids;

    Board() {
        for (std::size_t i = 0; i < node_count; ++i) {
            const core::Point at{core::X{16.f + 260.f * static_cast<float>(i % columns)},
                                 core::Y{16.f + 200.f * static_cast<float>(i / columns)}};
            ids.push_back(graph.add(blocks[i], {.title = "Block", .family = "math", .at = at}));
        }
        wire_up();
    }
    // Each block feeds the next two, so every input has one wire and the graph stays acyclic
    // by never wiring backwards: node i's output goes to i+1 input a and i+2 input b, up to wire_count.
    void wire_up() {
        std::size_t made = 0;
        for (std::size_t i = 0; i + 2 < node_count && made < wire_count; ++i) {
            made += graph.connect({ids[i], 2}, {ids[i + 1], 0}).has_value();
            made += graph.connect({ids[i], 2}, {ids[i + 2], 1}).has_value();
        }
    }
    void view(app::ViewBuilder& vb) { vb.node_board(graph); }
};

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double frame(WidgetTree& tree, uint64_t& version) {
    const auto t0 = std::chrono::steady_clock::now();
    auto snap = tree.build_snapshot(win_w, win_h, ++version);
    tree.clear_dirty();
    return ms_since(t0);
}

template <typename Change>
double mean_frame(WidgetTree& tree, uint64_t& version, Change change) {
    double total = 0;
    for (std::size_t i = 0; i < reps; ++i) {
        change(static_cast<int>(i));
        total += frame(tree, version);
    }
    return total / reps;
}

void report(const char* name, const char* at, float zoom, double ms) {
    fmt::print("{:<12} {:<4} zoom {:.2f}  {:>8.2f} ms{}\n", name, at, zoom, ms, ms > 16.0 ? "  (over 16 ms)" : "");
}

// The three cases from `view`, already set and drawn.
void run_at(Board& board, WidgetTree& tree, uint64_t& version, const char* at) {
    const auto view = board.graph.view();
    const float zoom = view.zoom;
    report("pan step", at, zoom, mean_frame(tree, version, [&](int i) {
               board.graph.set_view({view.pan + core::Offset{core::DX{static_cast<float>(i + 1) * 4}, core::DY{0}}, zoom});
           }));
    board.graph.set_view(view);
    report("zoom step", at, zoom, mean_frame(tree, version, [&](int i) {
               board.graph.set_view({view.pan, zoom * (i % 2 == 0 ? 1.1f : 1.f)});
           }));
    board.graph.set_view(view);
    report("slider", at, zoom, mean_frame(tree, version, [&](int i) {
               board.blocks[0].level.set({.value = i % 2 == 0 ? 0.25 : 0.75, .min = 0, .max = 1, .step = 0.05, .label = "Level"});
           }));
}
} // namespace

int main() {
    Board board;
    WidgetTree tree{board};
    uint64_t version = 0;
    frame(tree, version);
    fmt::print("mean ms per publish over {} publishes, {} nodes, {} wires, window {}x{}\n", reps, node_count,
               board.graph.edges().size(), win_w, win_h);
    for (float zoom : {1.f, 0.5f, 0.25f}) {
        board.graph.set_view({{}, zoom});
        frame(tree, version);
        run_at(board, tree, version, "");
    }
    board.graph.request_fit();
    frame(tree, version);   // fits in its layout
    frame(tree, version);   // draws the fitted view
    run_at(board, tree, version, "fit");
}
