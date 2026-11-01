#include "showcase_common.hpp"

struct DialogShowcase {
    prism::Field<prism::Menu> file{{.title = "File", .items = {"Save", "Reload"}}};
    prism::Field<prism::Label<>> text{{"Some edited text..."}};
    prism::Field<prism::Dialog> ask{{.title = "Unsaved changes",
                                     .message = "Reloading discards your edits.",
                                     .buttons = {"Discard", "Cancel"},
                                     .open = true}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
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
