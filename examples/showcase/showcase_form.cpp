#include "showcase_common.hpp"

using namespace prism;

enum class Quality { Draft, Normal, Best };

struct FormShowcase {
    core::Field<ui::Radio<Quality>> quality{{Quality::Normal}};
    core::Field<ui::RadioGroup> units{{.selected = 0, .options = {"Metric", "Imperial"}}};
    core::Field<ui::SpinBox<int>> copies{{.value = 3, .min = 1, .max = 99, .step = 1}};
    core::Field<ui::SpinBox<double>> scale{{.value = 1.25, .min = 0.5, .max = 4.0, .step = 0.25}};
    core::Field<ui::ProgressBar> done{{.value = 0.62, .label = "62% exported"}};
    core::Field<ui::ComboBox> format{{.value = "PNG", .placeholder = "Format",
                                     .options = {"PNG", "SVG", "PDF", "JPEG"}}};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.hstack(quality, units);
            vb.hstack(copies, scale);
            vb.widget(format);
            vb.widget(done);
        });
    }
};

int main(int argc, char* argv[]) {
    FormShowcase model;
    return showcase(argc, argv, model, 360, 260);
}
