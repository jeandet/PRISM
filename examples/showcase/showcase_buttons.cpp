#include "showcase_common.hpp"

using namespace prism;

struct Buttons {
    core::Field<ui::Button> save{{"Save"}};
    core::Field<ui::Button> cancel{{.text = "Cancel", .primary = false}};
    core::Field<ui::Button> bold{{.text = "Bold", .checked = true}};
    core::Field<ui::Checkbox> remember{{.checked = true, .label = "Remember me"}};

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(save);
            vb.gap(8);
            vb.widget(cancel);
            vb.gap(8);
            vb.widget(bold);
            vb.spacer();
        });
        vb.widget(remember);
    }
};

int main(int argc, char* argv[]) {
    Buttons model;
    return showcase(argc, argv, model, 360, 90);
}
