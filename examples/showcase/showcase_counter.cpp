#include "showcase_common.hpp"

using namespace prism;

struct Counter {
    core::Field<int> count{42};
    core::Field<std::string> label{"Hello, PRISM!"};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, label);
    }
};

int main(int argc, char* argv[]) {
    Counter model;
    return showcase(argc, argv, model, 300, 70);
}
