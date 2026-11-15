// Tour 2: a core::Derived is a value computed from fields, kept up to date for you.
#include <prism/prism.hpp>
#include <prism/app/run.hpp>

#include <string>

using namespace prism;

struct Rectangle {
    core::Field<ui::SpinBox<int>> width{{.value = 2, .min = 1, .max = 99}};
    core::Field<ui::SpinBox<int>> height{{.value = 3, .min = 1, .max = 99}};

    // Recomputes whenever either side changes; nothing has to call it, and a widget can show it.
    core::Derived<std::string> area{[this] {
        return "area: " + std::to_string(width.get().value * height.get().value);
    }, width, height};

    void view(app::WidgetTree::ViewBuilder& vb) { vb.vstack(width, height, area); }
};

int main() {
    Rectangle rectangle;
    return app::run(rectangle, {.title = "PRISM Tour 2: Derived Values",
                                .width = 520, .height = 120});
}

// At scale: examples/model_synth_rack/model_synth_rack.cpp's Link is one Derived over six blocks.
