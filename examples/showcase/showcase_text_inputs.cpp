#include "showcase_common.hpp"

using namespace prism;

struct TextInputs {
    core::Field<ui::TextField<>> name{{.value = "ada", .placeholder = "Name"}};
    core::Field<ui::Password<>> secret{{.value = "hunter2", .placeholder = "Password"}};
    core::Field<ui::ComboBox> fruit{{.value = "Ap", .placeholder = "Fruit",
                                     .options = {"Apple", "Apricot", "Banana"}}};
    core::Field<ui::TextArea<>> notes{{.value = "Several lines\nof editable text.", .rows = 3}};

    void view(app::ViewBuilder& vb) { vb.vstack(name, secret, fruit, notes); }
};

int main(int argc, char* argv[]) {
    TextInputs model;
    return showcase(argc, argv, model, 360, 220);
}
