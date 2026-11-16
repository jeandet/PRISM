// Plot: a live chart (drag to pan, wheel to zoom, right-click to fit) driven by sliders, and a
// canvas -- the escape hatch where an app draws its own thing -- redrawn when its inputs change.
#pragma once

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <vector>

namespace gallery {

struct PlotTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Plot and canvas", .heading = true}};
    prism::core::Field<prism::ui::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0, .step = 0.1, .label = "Frequency", .unit = "Hz"}};
    prism::core::Field<prism::ui::Slider<>> amplitude{{.value = 1.0, .min = 0.1, .max = 5.0, .step = 0.1, .label = "Amplitude"}};
    prism::core::Field<prism::ui::Checkbox> show_cos{{.checked = true, .label = "Show cosine"}};
    prism::core::Field<prism::ui::SpinBox<int>> ratio{{.value = 3, .min = 1, .max = 9, .step = 1}};
    prism::plot::PlotModel plot;
    prism::core::Wiring wire;

    PlotTab();
    // The canvas: a Lissajous figure with the spin box's frequency ratio.
    void canvas(prism::render::DrawList& dl, prism::core::Rect bounds, const prism::ui::WidgetNode& node);
    void view(prism::app::ViewBuilder& vb);   // plot_tab.cpp

private:
    void update();
};

} // namespace gallery
