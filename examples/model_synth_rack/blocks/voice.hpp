#pragma once

#include <prism/prism.hpp>

#include <fmt/format.h>
#include <string>

#include "../../widget_gallery/knob.hpp"
#include "../dsp/settings.hpp"

namespace prism::synth::blocks {

// The voice panel: which waveform and how it is shaped, from the filter's cutoff and Q to the
// gate envelope's two times.
struct Voice {
    core::Field<Waveform> waveform{Waveform::Saw};
    core::Field<gallery::Knob> cutoff{
        {.value = 2400.0, .min = 100.0, .max = 12000.0, .step = 10.0, .label = "Cutoff", .unit = "Hz"}};
    core::Field<gallery::Knob> resonance{
        {.value = 1.4, .min = 0.5, .max = 12.0, .step = 0.1, .label = "Resonance", .unit = "Q"}};
    core::Field<gallery::Knob> attack{
        {.value = 6.0, .min = 1.0, .max = 400.0, .step = 1.0, .label = "Attack", .unit = "ms"}};
    core::Field<gallery::Knob> release{
        {.value = 180.0, .min = 20.0, .max = 1200.0, .step = 5.0, .label = "Release", .unit = "ms"}};

    core::Derived<std::string> caption{
        [this] {
            return fmt::format("{} · cutoff {:.0f} Hz · Q {:.1f} · A {:.0f} / R {:.0f} ms",
                               ui::enum_label<Waveform>(ui::enum_index(waveform.get())),
                               value(cutoff), value(resonance), value(attack),
                               value(release));
        }};

    VoiceSettings settings() const {
        return {.waveform = waveform.get(),
                .cutoff_hz = value(cutoff),
                .resonance = value(resonance),
                .attack_ms = value(attack),
                .release_ms = value(release)};
    }

    void view(app::ViewBuilder& vb) {
        vb.widget(waveform);
        vb.hstack([&] {
            vb.widget(cutoff);
            vb.gap(6.f);
            vb.widget(resonance);
            vb.spacer();
        });
        vb.hstack([&] {
            vb.widget(attack);
            vb.gap(6.f);
            vb.widget(release);
            vb.spacer();
        });
        vb.widget(caption);
    }
};

} // namespace prism::synth::blocks
