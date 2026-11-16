// Tour 5: a block is a struct with fields and a view(); an app is blocks composed.
#include <prism/prism.hpp>

using namespace prism;

// One block: the oscillator's controls, laid out by itself.
struct Oscillator {
    core::Field<ui::Label<>> title{{"Oscillator"}};
    core::Field<ui::Slider<>> pitch{{.value = 0.5, .label = "Pitch"}};
    void view(app::ViewBuilder& vb) { vb.vstack(title, pitch); }
};

// Another: same shape, nothing shared between them.
struct Filter {
    core::Field<ui::Label<>> title{{"Filter"}};
    core::Field<ui::Slider<>> cutoff{{.value = 0.3, .label = "Cutoff"}};
    void view(app::ViewBuilder& vb) { vb.vstack(title, cutoff); }
};

// The app only composes: it never names a field of either block.
struct Voice {
    Oscillator osc;
    Filter filter;

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.block(osc);
            vb.gap(16.f);
            vb.block(filter);
        });
    }
};

int main() {
    Voice voice;
    return app::run(voice, {.title = "PRISM Tour 5: Blocks", .width = 560, .height = 200});
}

// At scale: examples/model_synth_rack/blocks/ is six of these, wired together by one settings Link.
