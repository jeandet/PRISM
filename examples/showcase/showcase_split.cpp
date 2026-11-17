#include "showcase_common.hpp"

using namespace prism;

struct SplitShowcase {
    core::Field<ui::TextArea<>> outline{{.value = "Drag the bar\nbetween the panes.", .rows = 5}};
    core::Field<ui::TextArea<>> body{{.value = "The user's sizes are saved in a field.", .rows = 5}};
    core::Field<ui::SplitSizes> sizes{};

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.remember_split(sizes);
            vb.widget(outline);
            vb.handle();
            vb.widget(body);
        });
    }
};

int main(int argc, char* argv[]) {
    SplitShowcase model;
    return showcase(argc, argv, model, 480, 140);
}
