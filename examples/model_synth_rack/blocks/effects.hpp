#pragma once

#include <prism/prism.hpp>

#include "../../widget_gallery/knob.hpp"
#include "../dsp/settings.hpp"

namespace prism::synth::blocks {

// The effects row: one feedback delay, three knobs. It has no caption of its own -- the voice
// panel it sits in already says what the chain is.
struct Effects {
    core::Field<gallery::Knob> time{
        {.value = 260.0, .min = 20.0, .max = 600.0, .step = 5.0, .label = "Delay", .unit = "ms"}};
    core::Field<gallery::Knob> feedback{
        {.value = 0.32, .min = 0.0, .max = 0.85, .step = 0.01, .label = "Feedback"}};
    core::Field<gallery::Knob> mix{{.value = 0.22, .min = 0.0, .max = 1.0, .step = 0.01, .label = "Mix"}};

    EffectsSettings settings() const {
        return {.delay_ms = value(time),
                .delay_feedback = value(feedback),
                .delay_mix = value(mix)};
    }

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(time);
            vb.gap(6.f);
            vb.widget(feedback);
            vb.gap(6.f);
            vb.widget(mix);
            vb.spacer();
        });
    }
};

} // namespace prism::synth::blocks
