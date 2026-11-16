#pragma once

#include <prism/prism.hpp>

#include <chrono>
#include <cmath>
#include <fmt/format.h>
#include <string>

#include "../daq/model.hpp"

namespace prism::lab::blocks {

// The status bar: the instrument's identity, the last frame's scalars, and how old that frame is.
// The age is what makes a stall visible: it climbs while the hardware thread waits.
struct StatusBar {
    explicit StatusBar(core::Field<DaqTelemetry>& shot) : frame{shot} {}

    core::Field<DaqTelemetry>& frame;   // the app's: it owns the frame, this bar reads it

    core::Derived<std::string> scalars{
        [this] {
            const DaqTelemetry& t = frame.get();
            if (t.fault_tripped) return fmt::format("OCP tripped  {:.0f} C", t.temperature_c);
            if (!t.scope_samples.empty() && t.measured_vpp == 0.0) return std::string{"output off"};
            return fmt::format("{:.2f}V  {:.2f}A  {:.1f}W  {:.0f}C", t.voltage_sense, t.current_sense,
                               t.power_watts, t.temperature_c);
        }};
    core::Field<ui::Label<>> device{{"simulated USB-TMC | /dev/usbtmc0"}};
    core::Field<ui::Label<>> age{{"frame age 0.0 s"}};

    // A frame arrived: the age restarts.
    void frame_arrived() { read_at_ = std::chrono::steady_clock::now(); }

    // Once per frame, on the logic thread. Tenths of a second: the label changes only when the age
    // does, so an idle bench is idle.
    void tick() {
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - read_at_).count();
        age.set({fmt::format("frame age {:.1f} s", std::floor(seconds * 10.0) / 10.0)});
    }

    // Called inside the bar rather than placed as a block: block() would wrap these four in one
    // container, and the spacer would stretch that container instead of spreading the bar.
    void view(app::ViewBuilder& vb) {
        vb.widget(device);
        vb.spacer();
        vb.widget(scalars);
        vb.gap(16.f);
        vb.widget(age);
    }

private:
    std::chrono::steady_clock::time_point read_at_{std::chrono::steady_clock::now()};
};

} // namespace prism::lab::blocks
