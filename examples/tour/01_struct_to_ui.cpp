// Tour 1: a struct of core::Field's is a window. With reflection, this is the whole program.
#include <prism/prism.hpp>

using namespace prism;

struct Settings {
    core::Field<ui::TextField<>> name{{.value = "PRISM"}};
    core::Field<ui::Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 1.0, .label = "Volume"}};
    core::Field<ui::Checkbox> mute{{.checked = false, .label = "Mute"}};

#if !__cpp_impl_reflection
    // Without reflection (the web build) the fields are placed by hand; the widgets are the same.
    void view(app::ViewBuilder& vb) { vb.vstack(name, volume, mute); }
#endif
};

int main() {
    Settings settings;
    return app::run(settings, {.title = "PRISM Tour 1: A Struct Is a Window",
                               .width = 420, .height = 160});
}

// At scale: examples/model_lab_bench/ builds a whole instrument out of structs like this one.
