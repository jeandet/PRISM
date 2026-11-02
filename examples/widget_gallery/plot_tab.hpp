// Plot: a live chart (drag to pan, wheel to zoom, right-click to fit) driven by sliders, and a
// canvas -- the escape hatch where an app draws its own thing -- redrawn when its inputs change.
#pragma once

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace gallery {

struct PlotTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Plot and canvas", .heading = true}};
    prism::core::Field<prism::ui::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    prism::core::Field<prism::ui::Slider<>> amplitude{{.value = 1.0, .min = 0.1, .max = 5.0}};
    prism::core::Field<prism::ui::Checkbox> show_cos{{.checked = true, .label = "Show cosine"}};
    prism::core::Field<prism::ui::SpinBox<int>> ratio{{.value = 3, .min = 1, .max = 9, .step = 1}};
    prism::plot::PlotModel plot;
    std::vector<prism::core::Connection> wiring;

    PlotTab() {
        plot.x_label.set("Time (rad)");
        plot.y_label.set("Amplitude");
        auto refresh = [this](const auto&) { update(); };
        wiring.push_back(frequency.on_change().connect(refresh));
        wiring.push_back(amplitude.on_change().connect(refresh));
        wiring.push_back(show_cos.on_change().connect(refresh));
        update();
    }

    // The canvas: a Lissajous figure with the spin box's frequency ratio.
    void canvas(prism::render::DrawList& dl, prism::core::Rect bounds, const prism::ui::WidgetNode& node) {
        using namespace prism::core;
        const auto& t = *node.theme;
        dl.rounded_rect(bounds, t.surface, 6.f);
        float cx = bounds.extent.w.raw() / 2, cy = bounds.extent.h.raw() / 2;
        float r = 0.42f * std::min(bounds.extent.w.raw(), bounds.extent.h.raw());
        std::vector<Point> pts;
        for (int i = 0; i <= 600; ++i) {
            double s = 2 * std::numbers::pi * i / 600;
            pts.push_back({X{cx + r * static_cast<float>(std::sin(ratio.get().value * s + 0.5))},
                           Y{cy + r * static_cast<float>(std::sin(2 * s))}});
        }
        dl.polyline(pts, t.accent, 2.f);
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.hstack([&] {
            vb.canvas(plot)
                .depends_on(plot.x_range)
                .depends_on(plot.y_range)
                .depends_on(plot.view)
                .depends_on(plot.cursor)
                .depends_on(plot.revision);
            vb.handle();
            vb.canvas(*this).depends_on(ratio);
        });
        vb.hstack([&] {
            vb.widget(frequency).tooltip("Frequency");
            vb.gap(8.f);
            vb.widget(amplitude).tooltip("Amplitude");
        });
        vb.hstack([&] {
            vb.widget(show_cos);
            vb.gap(16.f);
            vb.widget(ratio).tooltip("The canvas's frequency ratio");
            vb.spacer();
        });
    }

private:
    void update() {
        constexpr int n = 500;
        double f = frequency.get().value, a = amplitude.get().value;
        std::vector<double> xs(n), sin_ys(n), cos_ys(n);
        for (int i = 0; i < n; ++i) {
            xs[i] = static_cast<double>(i) / (n - 1) * 4.0 * std::numbers::pi;
            sin_ys[i] = a * std::sin(f * xs[i]);
            cos_ys[i] = a * std::cos(f * xs[i]);
        }
        auto colors = prism::plot::default_series_colors(prism::ui::default_theme());
        plot.clear_series();
        plot.add_series(prism::plot::XYData{xs, sin_ys}, prism::plot::SeriesStyle{colors[0], 2.f});
        if (show_cos.get().checked)
            plot.add_series(prism::plot::XYData{std::move(xs), std::move(cos_ys)}, prism::plot::SeriesStyle{colors[2], 2.f});
        plot.notify();
    }
};

} // namespace gallery
