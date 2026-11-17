#include "showcase_common.hpp"

using namespace prism;

struct Editor {
    core::Field<ui::Button> open{{.text = "Open", .primary = false}};
    core::Field<ui::Button> save{{.text = "Save"}};
    core::Field<ui::TextField<>> name{{.value = "notes.txt"}};
    core::Field<ui::Checkbox> wrap{{.checked = true, .label = "Wrap lines"}};
    core::Field<ui::Slider<>> zoom{{.value = 0.5, .label = "Zoom"}};
    core::Field<ui::Label<>> status{{"Ready"}};

    void view(app::ViewBuilder& vb) {
        vb.toolbar([&] { vb.widget(open); vb.widget(save); });
        vb.hstack([&] {
            vb.group("File", [&] { vb.widget(name); vb.widget(wrap); });
            vb.handle();
            vb.group("View", [&] { vb.widget(zoom); });
        });
        vb.status_bar([&] { vb.widget(status); });
    }
};

int main(int argc, char* argv[]) {
    Editor model;
    return showcase(argc, argv, model, 520, 240);
}
