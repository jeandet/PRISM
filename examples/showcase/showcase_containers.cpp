#include "showcase_common.hpp"

using namespace prism;

struct Containers {
    core::Field<ui::Checkbox> wrap{{.checked = true, .label = "Wrap long lines"}};
    core::Field<ui::SpinBox<int>> tab_width{{.value = 4, .min = 1, .max = 8}};
    core::Field<ui::Expander> advanced{{.title = "Advanced", .open = true}};
    core::Field<ui::Checkbox> verbose{{.label = "Verbose log"}};

    void view(app::ViewBuilder& vb) {
        vb.group("Editor", [&] {
            vb.widget(wrap);
            vb.widget(tab_width);
        });
        vb.section(advanced, [&](app::ViewBuilder& inner) { inner.widget(verbose); });
    }
};

int main(int argc, char* argv[]) {
    Containers model;
    return showcase(argc, argv, model, 320, 200);
}
