#include "showcase_common.hpp"

enum class Quality { Draft, Normal, Best };

struct FormShowcase {
    prism::Field<prism::Radio<Quality>> quality{{Quality::Normal}};
    prism::Field<prism::RadioGroup> units{{.selected = 0, .options = {"Metric", "Imperial"}}};
    prism::Field<prism::SpinBox<int>> copies{{.value = 3, .min = 1, .max = 99, .step = 1}};
    prism::Field<prism::SpinBox<double>> scale{{.value = 1.25, .min = 0.5, .max = 4.0, .step = 0.25}};
    prism::Field<prism::ProgressBar> done{{.value = 0.62, .label = "62% exported"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.hstack(quality, units);
            vb.hstack(copies, scale);
            vb.widget(done);
        });
    }
};

int main(int argc, char* argv[]) {
    FormShowcase model;
    return showcase(argc, argv, model, 360, 220);
}
