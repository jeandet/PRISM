#include "showcase_common.hpp"

using namespace prism;

struct Popover {
    core::Field<ui::Button> options{{.text = "Options", .primary = false}};
    core::Field<ui::Popup> panel{{.open = true, .modal = false}};
    core::Field<ui::Checkbox> grid{{.checked = true, .label = "Show grid"}};
    core::Field<ui::Checkbox> snap{{.label = "Snap to grid"}};

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(options);
            vb.popup(panel, [&](app::ViewBuilder& p) { p.vstack(grid, snap); });
            vb.spacer();
        });
        vb.spacer();
    }
};

int main(int argc, char* argv[]) {
    Popover model;
    return showcase(argc, argv, model, 320, 160);
}
