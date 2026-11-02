#include "showcase_common.hpp"

#include <cmath>
#include <numbers>

using namespace prism;

struct Waveform {
    core::Field<ui::Slider<>> frequency{{.value = 3.0, .min = 0.5, .max = 8.0}};

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        auto& t = *node.theme;
        float w = bounds.extent.w.raw();
        float h = bounds.extent.h.raw();
        float cy = h * 0.5f;
        float freq = static_cast<float>(frequency.get().value);

        dl.rounded_rect(bounds, t.surface, 6.f);
        dl.line({core::X{0}, core::Y{cy}}, {core::X{w}, core::Y{cy}}, t.track, 1.f);

        int steps = static_cast<int>(w);
        std::vector<core::Point> pts(static_cast<size_t>(steps));
        for (int i = 0; i < steps; ++i) {
            float x = static_cast<float>(i);
            float phase = freq * x / w;
            float y = cy - 0.4f * h * std::sin(2.f * std::numbers::pi_v<float> * phase);
            pts[static_cast<size_t>(i)] = {core::X{x}, core::Y{y}};
        }
        dl.polyline(pts, t.accent, 2.f);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(frequency);
        vb.widget(frequency);
    }
};

int main(int argc, char* argv[]) {
    Waveform model;
    return showcase(argc, argv, model, 400, 250);
}
