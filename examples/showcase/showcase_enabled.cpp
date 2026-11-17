#include "showcase_common.hpp"

using namespace prism;

struct Advanced {
    core::Field<ui::Checkbox> unlocked{{.checked = false, .label = "Advanced options"}};
    core::Field<ui::Slider<>> gain{{.value = 0.5, .label = "Gain"}};
    core::Field<ui::SpinBox<int>> taps{{.value = 16, .min = 1, .max = 64}};
    core::Field<ui::TextField<>> name{{.placeholder = "File name"}};
    core::Field<ui::Button> save{{"Save"}};
    core::Derived<bool> has_name{[this] { return !value(name).empty(); }};

    void view(app::ViewBuilder& vb) {
        vb.widget(unlocked);
        vb.enabled_if(unlocked, [&] { vb.vstack(gain, taps); });
        vb.hstack([&] {
            vb.widget(name);
            vb.widget(save).enabled(has_name);
        });
    }
};

int main(int argc, char* argv[]) {
    Advanced model;
    return showcase(argc, argv, model, 360, 170);
}
