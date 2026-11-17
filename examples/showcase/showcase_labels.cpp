#include "showcase_common.hpp"

using namespace prism;

struct Labels {
    core::Field<ui::Label<>> title{{.value = "Export", .heading = true}};
    core::Field<ui::Label<>> help{{.value = "The copy goes next to the original, "
                                            "which stays as it was.", .wrap = true}};
    core::Field<ui::Link> docs{{.text = "Read the docs", .url = "https://prism-gui.readthedocs.io"}};
    core::Field<ui::ProgressBar> done{{.value = 0.62, .label = "62% exported"}};
    core::Field<ui::ProgressBar> waiting{{.label = "Connecting", .busy = true}};

    void view(app::ViewBuilder& vb) { vb.vstack(title, help, docs, done, waiting); }
};

int main(int argc, char* argv[]) {
    Labels model;
    return showcase(argc, argv, model, 320, 220);
}
