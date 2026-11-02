// Perf-lab benchmark: what one frame costs with N ordinary widgets (doc/review-2026-11-02.md P3
// #11). Layout runs over the whole tree on every publish; this measures whether that matters.
//
// The window holds N widgets in rows of five (label, slider, checkbox, text field, button) inside
// a scroll area, as a long settings page or inspector would. Each case times what model_app does
// per publish: build_snapshot() then clear_dirty(). No threads, no backend: the tree alone.
//
//   ./builddir/benchmarks/bench_widget_scale            # N = 1000, 10000, 50000
//   ./builddir/benchmarks/bench_widget_scale 20000      # one N
#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace prism;
using app::WidgetTree;

namespace {
struct Row {
    core::Field<ui::Label<>> name{{"setting"}};
    core::Field<ui::Slider<>> level{{.value = 0.5}};
    core::Field<ui::Checkbox> on{{.checked = false, .label = "on"}};
    core::Field<ui::TextField<>> note{{.value = "note"}};
    core::Field<ui::Button> reset{{"Reset"}};
};
constexpr std::size_t widgets_per_row = 5;

struct Page {
    std::unique_ptr<Row[]> rows;   // Fields must not move once the tree points at them
    std::size_t count;
    explicit Page(std::size_t widgets)
        : rows(std::make_unique<Row[]>(widgets / widgets_per_row)), count(widgets / widgets_per_row) {}
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            for (std::size_t i = 0; i < count; ++i) {
                auto& r = rows[i];
                vb.hstack(r.name, r.level, r.on, r.note, r.reset);
            }
        });
    }
};

constexpr float win_w = 1280.f, win_h = 800.f;

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// One publish, as model_app does it.
double frame(WidgetTree& tree, uint64_t& version) {
    auto t0 = std::chrono::steady_clock::now();
    auto snap = tree.build_snapshot(win_w, win_h, ++version);
    tree.clear_dirty();
    return ms_since(t0);
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Median frame time over `reps` repetitions of change-then-publish.
template <typename Change>
double median_frame(WidgetTree& tree, uint64_t& version, int reps, Change change) {
    std::vector<double> times;
    for (int i = 0; i < reps; ++i) {
        change(i);
        times.push_back(frame(tree, version));
    }
    return median(times);
}

void run(std::size_t widgets) {
    Page page(widgets);
    auto t0 = std::chrono::steady_clock::now();
    WidgetTree tree{page};
    double construct = ms_since(t0);
    uint64_t version = 0;
    double first = frame(tree, version);
    auto probe = tree.build_snapshot(win_w, win_h, ++version);
    tree.clear_dirty();
    constexpr int reps = 50;

    double nothing = median_frame(tree, version, reps, [](int) {});
    // A value the user sees change, same size: a checkbox toggling at the bottom of the page.
    double toggle = median_frame(tree, version, reps, [&](int i) {
        auto& c = page.rows[page.count - 1].on;
        c.set({.checked = i % 2 == 0, .label = "on"});
    });
    // A label whose text grows and shrinks: its row's width changes.
    double relabel = median_frame(tree, version, reps, [&](int i) {
        page.rows[0].name.set({i % 2 == 0 ? "a much longer setting name" : "setting"});
    });
    // The wheel over the page: every publish moves the visible window.
    auto snap = tree.build_snapshot(win_w, win_h, ++version);
    tree.clear_dirty();
    core::Point centre{core::X{win_w / 2}, core::Y{win_h / 2}};
    double scroll = median_frame(tree, version, reps, [&](int i) {
        app::widget_detail::route_event(tree, snap.get(), input::MouseScroll{centre, core::DX{0}, core::DY{i % 2 == 0 ? -3.f : 3.f}});
    });

    fmt::print("{:>7} {:>10.1f} {:>10.1f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f} {:>10} {:>10}\n", widgets,
               construct, first, nothing, toggle, relabel, scroll, probe->draw_command_count,
               probe->geometry.size());
}
} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::size_t> sizes{1000, 10000, 50000};
    if (argc > 1) sizes = {static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))};
    fmt::print("median ms per publish (build_snapshot + clear_dirty), window {}x{}\n", win_w, win_h);
    fmt::print("{:>7} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10}\n", "widgets", "construct",
               "first", "unchanged", "toggle", "relabel", "scroll", "snap cmds", "hit rects");
    for (auto n : sizes) run(n);
}
