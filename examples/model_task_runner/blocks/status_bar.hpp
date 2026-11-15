#pragma once

// The status bar: three numbers about everything the consoles hold between them -- how many runs
// are going, what the console costs in lines per second, and how many lines are held.

#include <prism/prism.hpp>

#include "../process/clock.hpp"
#include "../process/text.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

#include <fmt/format.h>

namespace prism::runner::blocks {

struct StatusBar {
    core::Field<ui::Label<>> runs{{"idle   0 runs"}};
    core::Field<ui::Label<>> throughput{{"0 lines/s"}};
    core::Field<ui::Label<>> held{{"0 lines held"}};

    void refresh(steady::time_point now, unsigned running, unsigned total_runs, std::size_t lines) {
        const double dt = std::chrono::duration<double>(now - rate_at_).count();
        if (dt >= 0.25) {
            // Signed: a relaunch empties a console, and the count must not wrap around.
            const double fresh = (static_cast<double>(lines) - static_cast<double>(rate_lines_)) / dt;
            lines_per_second_ = lines_per_second_ * 0.5 + fresh * 0.5;
            rate_lines_ = lines;
            rate_at_ = now;
        }
        const auto rate = static_cast<std::size_t>(std::lround(std::max(0.0, lines_per_second_)));
        show(running, total_runs, lines, rate);
    }

    void show(unsigned running, unsigned total_runs, std::size_t lines, std::size_t rate) {
        runs.set({running == 0 ? fmt::format("idle    {} runs", total_runs)
                               : fmt::format("{} running    {} runs", running, total_runs)});
        throughput.set({fmt::format("{} lines/s", grouped(rate))});
        held.set({fmt::format("{} lines held", grouped(lines))});
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.widget(runs);
        vb.spacer();
        vb.widget(throughput);
        vb.gap(16.f);
        vb.widget(held);
    }

private:
    steady::time_point rate_at_{steady::now()};
    double lines_per_second_ = 0.0;
    std::size_t rate_lines_ = 0;
};

} // namespace prism::runner::blocks
