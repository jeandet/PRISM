#include "showcase_common.hpp"

using namespace prism;

struct AudioMixer {
    core::Field<ui::Slider<>> volume{{.value = 0.75, .min = 0.0, .max = 1.0}};
    core::Field<ui::Slider<int>> quality{{.value = 3, .min = 1, .max = 5, .step = 1}};
    core::Field<ui::Checkbox> mute{{.checked = false, .label = "Mute"}};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack(volume, quality, mute);
    }
};

int main(int argc, char* argv[]) {
    AudioMixer model;
    return showcase(argc, argv, model, 400, 100);
}
