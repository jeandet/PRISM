#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <string>
#include <vector>

// A simulated bench instrument: a DC power supply with a function generator and a scope on its
// output. Nothing here knows about threads, I/O or the UI -- every function is a pure function
// of the settings and the clock, so tests/test_lab_bench.cpp exercises it directly.
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
    double captured_at = 0.0;                  // the instrument's clock when this frame was read
    bool operator==(const DaqTelemetry&) const = default;
};

struct DaqEvent {
    std::string timestamp;
    std::string message;
    bool is_error = false;
};

// Pure math: one sample of a normalized waveform in [-1, 1] at `phase` (in cycles).
inline float sample_normalized_waveform(WaveformType type, double phase, double noise_val = 0.0) {
    phase = phase - std::floor(phase); // wrap to [0, 1)
    switch (type) {
        case WaveformType::Sine:
            return static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
        case WaveformType::Square:
            return phase < 0.5 ? 1.0f : -1.0f;
        case WaveformType::Triangle:
            return static_cast<float>(phase < 0.5 ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase));
        case WaveformType::Sawtooth:
            return static_cast<float>(2.0 * phase - 1.0);
        case WaveformType::Noise:
            return std::clamp(static_cast<float>(noise_val), -1.0f, 1.0f);
    }
    return 0.0f;
}

// The 1-2-5 step a scope's volts/div knob lands on: the smallest of those not below `raw`.
inline double nice_step(double raw) {
    if (!(raw > 0.0)) return 1.0;
    double base = std::pow(10.0, std::floor(std::log10(raw)));
    double m = raw / base;
    double step = m <= 1.0 ? 1.0 : (m <= 2.0 ? 2.0 : (m <= 5.0 ? 5.0 : 10.0));
    return step * base;
}

// Pure math: one frame of telemetry at `time_sec`.
//
// simplify: the load is one fixed 8 ohm resistor with a slow drift, the supply's thermal model
// is a single gain, and the sweep is fixed at kScopeWindowSeconds (no time/div knob). Each of
// the three would be a real feature on a real instrument; none of them is needed to show what
// this example is about. Upgrade path: add `load_ohms` and `timebase_sec` to DaqSettings and
// read them here -- the UI needs no change beyond one more control each.
inline DaqTelemetry synthesize_telemetry(const DaqSettings& s, double time_sec,
                                         double window_sec = kScopeWindowSeconds,
                                         std::size_t sample_count = kScopeSamples) {
    DaqTelemetry t;
    t.captured_at = time_sec;
    t.wave_type = s.wave_type;
    t.time_per_div = window_sec / static_cast<double>(kScopeHDivisions);
    t.scope_samples.assign(sample_count, 0.0f);
    if (sample_count < 2) return t;   // nothing to draw, and dt would be a division by zero

    if (s.simulate_fault) {
        t.fault_tripped = true;
        t.current_sense = s.current_limit * 1.25;   // over the limit
        t.temperature_c = 65.0;
        return t;
    }

    if (!s.output_enabled) {
        t.voltage_sense = 0.02 * std::sin(time_sec * 2.0);   // a quiet output, not a clean zero
        t.temperature_c = 25.0 + 2.0 * std::sin(time_sec * 0.1);
        return t;
    }

    const double load_r = 8.0 + 0.5 * std::sin(time_sec * 0.3);
    t.constant_current = s.target_voltage / load_r > s.current_limit;
    if (t.constant_current) {
        t.current_sense = s.current_limit;
        t.voltage_sense = s.current_limit * load_r;
    } else {
        t.voltage_sense = s.target_voltage;
        t.current_sense = s.target_voltage / load_r;
    }
    t.power_watts = t.voltage_sense * t.current_sense;
    t.temperature_c = 28.0 + t.power_watts * 0.85;

    // Edge trigger: put a rising edge at the middle of the sweep, so the trace stands still
    // when the frame is re-read while the generator keeps running.
    const double trigger_t = std::round(time_sec * s.frequency_hz) / s.frequency_hz;
    const double t0 = trigger_t - window_sec * 0.5;
    const double dt = window_sec / static_cast<double>(sample_count - 1);
    const double amplitude = s.amplitude_vpp * 0.5;

    // Noise is the only waveform that needs random samples: seeding a generator per frame for
    // the four deterministic ones was both wasted work and a false claim on the screen.
    std::mt19937 rng(static_cast<unsigned int>(std::lround(time_sec * 100.0)));
    std::normal_distribution<double> dist(0.0, 0.4);

    float peak = 0.0f;
    for (std::size_t i = 0; i < sample_count; ++i) {
        double phase = (t0 + static_cast<double>(i) * dt) * s.frequency_hz;
        double noise = s.wave_type == WaveformType::Noise ? dist(rng) : 0.0;
        float v = static_cast<float>(s.offset_v + amplitude * sample_normalized_waveform(s.wave_type, phase, noise));
        t.scope_samples[i] = v;
        peak = std::max(peak, std::abs(v));
    }
    t.v_per_div = nice_step(static_cast<double>(peak) / (kScopeVDivisions / 2.0));

    const auto [lo, hi] = std::minmax_element(t.scope_samples.begin(), t.scope_samples.end());
    t.measured_vpp = static_cast<double>(*hi - *lo);

    double sum_sq = 0.0;
    std::size_t rising = 0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        double v = static_cast<double>(t.scope_samples[i]);
        sum_sq += v * v;
        if (i > 0 && t.scope_samples[i - 1] <= s.offset_v && v > s.offset_v) ++rising;
    }
    t.measured_vrms = std::sqrt(sum_sq / static_cast<double>(sample_count));
    // Below one cycle per sweep a count cannot resolve the frequency: report the setpoint, and
    // say so (this is why a real scope has a timebase knob).
    t.measured_freq = rising >= 2 ? static_cast<double>(rising) / window_sec : s.frequency_hz;
    return t;
}

} // namespace prism::lab
