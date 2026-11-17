#include "showcase_common.hpp"

using namespace prism;

struct ClickCounter {
    core::Field<int> count{0};
    core::Field<ui::Button> add{{"Add one"}};

    ClickCounter() {
        add.observe([this](const ui::Button&) { ++count; });
    }

    void view(app::ViewBuilder& vb) { vb.vstack(count, add); }
};

int main(int argc, char* argv[]) {
    ClickCounter model;
    return showcase(argc, argv, model, 300, 80);
}
