#include "showcase_common.hpp"

using namespace prism;

// The same few widgets as showcase_theme_dark.cpp, drawn with ui::light_theme().
struct ThemeSample {
    core::Field<ui::TextField<>> name{{.value = "ada", .placeholder = "Name"}};
    core::Field<ui::Slider<>> volume{{.value = 60, .min = 0, .max = 100, .step = 1, .label = "Volume", .unit = "%"}};
    core::Field<ui::Checkbox> notify{{.checked = true, .label = "Notify me"}};
    core::Field<ui::Button> save{{.text = "Save"}};

    void view(app::ViewBuilder& vb) {
        vb.vstack(name, volume, notify, save);
    }
};

int main(int argc, char* argv[]) {
    ThemeSample model;
    return showcase(argc, argv, model, 320, 200,
                    [](app::AppContext& ctx) { ctx.set_theme(ui::light_theme()); });
}
