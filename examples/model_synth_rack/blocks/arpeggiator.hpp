#pragma once

#include <prism/prism.hpp>

#include <cmath>
#include <cstdint>
#include <fmt/format.h>
#include <string>

#include "../../widget_gallery/knob.hpp"
#include "../dsp/arpeggiator.hpp"
#include "../dsp/settings.hpp"

namespace prism::synth::blocks {

// The arpeggiator panel: run/stop, tempo, octaves, chord and pattern. `power` is only a request
// the app acts on -- opening an audio device is not a panel's business -- everything else here
// is a plain value the renderer reads.
struct Arpeggiator {
    core::Field<ui::Button> power{{.text = "Power", .primary = true, .checked = false}};
    core::Field<gallery::Knob> tempo{
        {.value = 96.0, .min = 60.0, .max = 200.0, .step = 1.0, .label = "Tempo", .unit = "BPM"}};
    core::Field<Chord> chord{Chord::Cmaj7};
    core::Field<ArpPattern> pattern{ArpPattern::Up};
    core::Field<gallery::Knob> octaves{
        {.value = 2.0, .min = 1.0, .max = 3.0, .step = 1.0, .label = "Octaves", .unit = "oct"}};

    core::Derived<std::string> caption{
        [this] {
            return fmt::format("{} · {} · {:.0f} BPM · {} oct · {} notes",
                               ui::enum_label<Chord>(ui::enum_index(chord.get())),
                               ui::enum_label<ArpPattern>(ui::enum_index(pattern.get())),
                               value(tempo), octave_span(),
                               arp_sequence(chord.get(), octave_span()).count);
        }};

    bool powered() const { return value(power); }

    ArpSettings settings(std::uint16_t step_mask) const {
        return {.powered = powered(),
                .tempo_bpm = value(tempo),
                .chord = chord.get(),
                .pattern = pattern.get(),
                .octaves = octave_span(),
                .step_mask = step_mask};
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            vb.widget(power).tooltip("Open the audio device and start the engine");
            vb.hstack([&] {
                vb.widget(tempo);
                vb.gap(8.f);
                vb.widget(octaves);
                vb.spacer();
            });
            vb.widget(chord);
            vb.widget(pattern);
            vb.widget(caption);
        });
    }

private:
    int octave_span() const { return static_cast<int>(std::lround(value(octaves))); }
};

} // namespace prism::synth::blocks
