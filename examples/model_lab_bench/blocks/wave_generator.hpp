#pragma once

#include <prism/prism.hpp>

#include <fmt/format.h>
#include <string>

#include "../daq/model.hpp"

namespace prism::lab::blocks {

// The signal generator panel: waveform, frequency, amplitude and offset, plus the period readout
// that follows from the frequency and the scope's fixed sweep.
struct WaveGenerator {
    core::Field<WaveformType> wave_type{WaveformType::Sine};
    core::Field<ui::Slider<double>> frequency{
        {.value = 1000.0, .min = 50.0, .max = 20000.0, .step = 50.0, .label = "Frequency", .unit = "Hz"}};
    core::Field<ui::Slider<double>> amplitude{
        {.value = 5.0, .min = 0.5, .max = 10.0, .step = 0.1, .label = "Amplitude", .unit = "Vpp"}};
    core::Field<ui::Slider<double>> offset{
        {.value = 0.0, .min = -4.0, .max = 4.0, .step = 0.1, .label = "Offset", .unit = "V"}};

    core::Derived<std::string> period_caption{
        [this] {
            return fmt::format("Period  {:.3f} ms   {:.1f} cycles on screen", 1e3 / frequency.get().value,
                               frequency.get().value * kScopeWindowSeconds);
        },
        frequency};

    void apply_to(DaqSettings& settings) const {
        settings.wave_type = wave_type.get();
        settings.frequency_hz = frequency.get().value;
        settings.amplitude_vpp = amplitude.get().value;
        settings.offset_v = offset.get().value;
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(wave_type).tooltip("Signal generator waveform shape");
            vb.widget(frequency);
            vb.widget(amplitude);
            vb.widget(offset);
            vb.widget(period_caption);
        });
    }
};

} // namespace prism::lab::blocks
