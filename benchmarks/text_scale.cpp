// How TextArea copes with a long text (doc/design/text-stack.md, step 1): the numbers a document
// model has to beat. One TextArea in the window, N lines of code-like text in it. Each case times
// the input (the delegate's handling: copy, edit, re-wrap) and the publish that follows.
// No backend: text is measured with the no-font estimate, so real fonts only cost more.
//
//   ./builddir-release/benchmarks/bench_text_scale            # N = 1000, 10000, 100000
//   ./builddir-release/benchmarks/bench_text_scale 50000      # one N
#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

using namespace prism;
using app::WidgetTree;

namespace {
constexpr float win_w = 1280.f, win_h = 800.f;

std::string lines_of_code(std::size_t n) {
    std::string text;
    text.reserve(n * 48);
    for (std::size_t i = 0; i < n; ++i)
        text += fmt::format("    auto value_{} = compute(input[{}], {});\n", i, i % 97, i * 3);
    return text;
}

struct Page {
    core::Field<ui::TextArea<>> code;
    explicit Page(std::size_t lines) : code{{.value = lines_of_code(lines), .rows = 40}} {}
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(code); }
};

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Cost { double input, publish; };

// Median over `reps` of: send one event, then publish as model_app does.
Cost median_cost(WidgetTree& tree, uint64_t& version, int reps, const input::InputEvent& ev) {
    std::vector<double> inputs, publishes;
    for (int i = 0; i < reps; ++i) {
        auto snap = tree.build_snapshot(win_w, win_h, ++version);
        tree.clear_dirty();
        auto t0 = std::chrono::steady_clock::now();
        app::widget_detail::route_event(tree, snap.get(), ev);
        inputs.push_back(ms_since(t0));
        t0 = std::chrono::steady_clock::now();
        auto next = tree.build_snapshot(win_w, win_h, ++version);
        tree.clear_dirty();
        publishes.push_back(ms_since(t0));
    }
    return {median(inputs), median(publishes)};
}

void run(std::size_t lines) {
    Page page(lines);
    WidgetTree tree{page};
    uint64_t version = 0;
    auto t0 = std::chrono::steady_clock::now();
    auto snap = tree.build_snapshot(win_w, win_h, ++version);
    tree.clear_dirty();
    const double first = ms_since(t0);

    const core::Point inside{core::X{200}, core::Y{100}};
    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{inside, 1, true});
    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{inside, 1, false});

    constexpr int reps = 15;
    const Cost nothing = [&] {
        std::vector<double> t;
        for (int i = 0; i < reps; ++i) {
            auto s = std::chrono::steady_clock::now();
            auto n = tree.build_snapshot(win_w, win_h, ++version);
            tree.clear_dirty();
            t.push_back(ms_since(s));
        }
        return Cost{0, median(t)};
    }();
    const Cost type = median_cost(tree, version, reps, input::TextInput{"x"});
    const Cost down = median_cost(tree, version, reps, input::KeyPress{input::keys::down, 0});
    const Cost wheel = median_cost(tree, version, reps, input::MouseScroll{inside, core::DX{0}, core::DY{-3}});

    fmt::print("{:>7} {:>8.1f} {:>8.2f} {:>8.2f}+{:<8.2f} {:>8.2f}+{:<8.2f} {:>8.2f}+{:<8.2f}\n", lines,
               first, nothing.publish, type.input, type.publish, down.input, down.publish, wheel.input,
               wheel.publish);
}
} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::size_t> sizes{1000, 10000, 100000};
    if (argc > 1) sizes = {static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))};
    fmt::print("TextArea, median ms: input handling + publish (build_snapshot + clear_dirty)\n");
    fmt::print("{:>7} {:>8} {:>8} {:>17} {:>17} {:>17}\n", "lines", "first", "nothing", "type a key",
               "arrow down", "wheel");
    for (auto n : sizes) run(n);
}
