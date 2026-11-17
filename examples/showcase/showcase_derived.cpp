#include "showcase_common.hpp"

#include <string>

using namespace prism;

struct Rectangle {
    core::Field<ui::SpinBox<int>> width{{.value = 2, .min = 1, .max = 99}};
    core::Field<ui::SpinBox<int>> height{{.value = 3, .min = 1, .max = 99}};
    core::Derived<std::string> area{[this] {
        return "area: " + std::to_string(value(width) * value(height));
    }};

    void view(app::ViewBuilder& vb) { vb.vstack(width, height, area); }
};

int main(int argc, char* argv[]) {
    Rectangle model;
    return showcase(argc, argv, model, 360, 110);
}
