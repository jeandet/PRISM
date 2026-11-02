#include "showcase_common.hpp"
#include <prism/widgets/plot.hpp>

#include <cmath>
#include <numbers>

using namespace prism;

struct PlotShowcase {
    plot::PlotModel plot;

    void setup_data() {
        constexpr int N = 200;
        std::vector<double> xs(N), ys(N);
        for (int i = 0; i < N; ++i) {
            double t = static_cast<double>(i) / (N - 1) * 4.0 * std::numbers::pi;
            xs[static_cast<size_t>(i)] = t;
            ys[static_cast<size_t>(i)] = std::sin(t) * std::exp(-t * 0.1);
        }

        auto colors = plot::default_series_colors(ui::default_theme());
        plot.add_series(plot::XYData{std::move(xs), std::move(ys)},
                        plot::SeriesStyle{colors[0], 2.f});
        plot.x_label.set("Time (s)");
        plot.y_label.set("Amplitude");
        plot.notify();
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.canvas(plot)
            .depends_on(plot.x_range)
            .depends_on(plot.y_range)
            .depends_on(plot.view)
            .depends_on(plot.cursor)
            .depends_on(plot.revision);
    }
};

int main(int argc, char* argv[]) {
    PlotShowcase model;
    model.setup_data();
    return showcase(argc, argv, model, 600, 400);
}
