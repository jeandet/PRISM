#pragma once

#include <prism/prism.hpp>

#include <fmt/format.h>
#include <string>
#include <vector>

#include "../daq/model.hpp"
#include "event_log.hpp"

namespace prism::lab::blocks {

// The DC power supply panel: the two setpoints, the enable button, and what the last frame says it
// is doing. It contributes its slice of DaqSettings and writes a log line when the output moves.
struct PowerSupply {
    PowerSupply(core::Field<DaqTelemetry>& shot, EventLog& events) : frame{shot}, log{events} {
        reactions_.push_back(output_toggle.on_change().connect([this](const ui::Button& button) {
            log.push(button.checked.value_or(false) ? "DC output enabled" : "DC output disabled");
        }));
    }

    core::Field<DaqTelemetry>& frame;   // the app's: read by the mode caption
    EventLog& log;

    core::Field<ui::Button> output_toggle{{.text = "Output", .primary = true, .checked = true}};
    core::Field<ui::Slider<double>> target_voltage{
        {.value = 12.0, .min = 0.0, .max = 30.0, .step = 0.1, .label = "Voltage", .unit = "V"}};
    core::Field<ui::Slider<double>> current_limit{
        {.value = 2.0, .min = 0.1, .max = 5.0, .step = 0.05, .label = "Current limit", .unit = "A"}};

    // What the sliders cannot print themselves: values computed from several of them, or from the
    // frame the supply just made.
    core::Derived<std::string> power_caption{
        [this] {
            return fmt::format("Max power {:>5.1f} W", target_voltage.get().value * current_limit.get().value);
        }};
    core::Derived<std::string> mode_caption{
        [this] {
            const DaqTelemetry& t = frame.get();
            if (t.fault_tripped) return std::string{"Mode  protection tripped"};
            if (!(t.current_sense > 0.0)) return std::string{"Mode  output off"};
            return fmt::format("Mode  {}   load {:.2f} ohm", t.constant_current ? "CC" : "CV",
                               t.voltage_sense / t.current_sense);
        }};

    void apply_to(DaqSettings& settings) const {
        settings.target_voltage = target_voltage.get().value;
        settings.current_limit = current_limit.get().value;
        settings.output_enabled = output_toggle.get().checked.value_or(false);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(output_toggle).tooltip("Enable or disable the DC output");
            vb.widget(target_voltage);
            vb.widget(current_limit);
            vb.widget(power_caption);
            vb.widget(mode_caption);
        });
    }

private:
    // Declared last, so the connection goes before the field it observes.
    std::vector<core::Connection> reactions_{};
};

} // namespace prism::lab::blocks
