#include "showcase_common.hpp"

using namespace prism;

struct Settings {
    core::Field<std::string> username{"ada"};
    core::Field<bool> dark_mode{true};

    void view(app::ViewBuilder& vb) {
        vb.vstack(username, dark_mode);
    }
};

struct Dashboard {
    Settings settings;
    core::Field<ui::Slider<>> brightness{{.value = 0.6, .min = 0.0, .max = 1.0}};
    core::Field<ui::Button> apply{{"Apply"}};

    void view(app::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.block(settings);
            vb.hstack(brightness, apply);
        });
    }
};

int main(int argc, char* argv[]) {
    Dashboard model;
    return showcase(argc, argv, model, 400, 100);
}
