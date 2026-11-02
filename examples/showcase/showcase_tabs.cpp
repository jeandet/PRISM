#include "showcase_common.hpp"

using namespace prism;

struct TabsShowcase {
    core::Field<ui::TextField<>> username{{.value = "jeandet"}};
    core::Field<bool> dark_mode{true};
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
    core::Field<ui::TabBar<>> tabs;

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Account", [&](app::WidgetTree::ViewBuilder& tvb) {
                tvb.vstack(username, dark_mode);
            });
            vb.tab("Audio", [&](app::WidgetTree::ViewBuilder& tvb) {
                tvb.widget(volume);
            });
        });
    }
};

int main(int argc, char* argv[]) {
    TabsShowcase model;
    return showcase(argc, argv, model, 320, 140);
}
