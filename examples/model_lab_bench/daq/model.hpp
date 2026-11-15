#pragma once

#include <cstddef>
#include <string>
#include <vector>

// What the bench instrument measures, with no PRISM in it: the settings its front panel produces,
// the telemetry one read returns, and the events it reports. tests/test_lab_bench.cpp builds and
// checks these directly.
namespace prism::lab {

enum class WaveformType { Sine, Square, Triangle, Sawtooth, Noise };

// The scope's sweep: a real timebase, not a number of periods. A fixed number of periods would
// make the frequency control a no-op on screen (every frequency would fill the screen equally).
inline constexpr double kScopeWindowSeconds = 0.002;   // 2 ms across 10 divisions: 200 us/div
inline constexpr std::size_t kScopeSamples = 401;      // 5 us apart at the sweep above
inline constexpr int kScopeVDivisions = 8;             // vertical divisions on the graticule
inline constexpr int kScopeHDivisions = 10;

struct DaqSettings {
    double target_voltage = 12.0;              // V (0 - 30)
    double current_limit = 2.0;                // A (0.1 - 5)
    bool output_enabled = true;
    WaveformType wave_type = WaveformType::Sine;
    double frequency_hz = 1000.0;              // Hz (50 - 20000)
    double amplitude_vpp = 5.0;                // V, peak to peak (the generator's amplitude knob)
    double offset_v = 0.0;                     // V, DC offset (-4 - 4)
    bool simulate_fault = false;               // trip the over-current protection
    bool operator==(const DaqSettings&) const = default;
};

struct DaqTelemetry {
    // Slow scalars: the power supply's readout.
    double voltage_sense = 0.0;
    double current_sense = 0.0;
    double power_watts = 0.0;
    double temperature_c = 25.0;
    bool constant_current = false;
    bool fault_tripped = false;

    // One scope frame.
    std::vector<float> scope_samples{};
    WaveformType wave_type = WaveformType::Sine;
    double measured_vpp = 0.0;
    double measured_vrms = 0.0;
    double measured_freq = 0.0;
    // What the screen is drawn at. The header prints these, so the printed scale cannot drift
    // from the geometry actually drawn (a hard-coded "2.00 V/div" over an autoscaled trace is
    // the classic way a fake scope gives itself away).
    double v_per_div = 1.0;
    double time_per_div = kScopeWindowSeconds / kScopeHDivisions;
    bool operator==(const DaqTelemetry&) const = default;
};

// A state change the instrument saw, reported through the Channel. No timestamp: the log stamps a
// line when it receives one, which is the same moment on the same clock as every other line.
struct DaqEvent {
    std::string message;
    bool is_error = false;
};

} // namespace prism::lab
