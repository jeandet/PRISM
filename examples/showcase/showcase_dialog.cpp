#include "showcase_common.hpp"

using namespace prism;

struct DialogShowcase {
    core::Field<ui::Menu> file{{.title = "File", .items = {"Save", "Reload"}}};
    core::Field<ui::Label<>> text{{"Some edited text..."}};
    core::Field<ui::Dialog> ask{{.title = "Unsaved changes",
                                     .message = "Reloading discards your edits.",
                                     .buttons = {"Discard", "Cancel"},
                                     .open = true}};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(file);
            vb.widget(text);
            vb.dialog(ask);
        });
    }
};

int main(int argc, char* argv[]) {
    DialogShowcase model;
    return showcase(argc, argv, model, 480, 260);
}
