#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_lab_bench/lab_bench_model.hpp"

#include <cmath>

using namespace prism::lab;

TEST_CASE("normalized waveform sampling bounds") {
    // Sine should stay in [-1, 1]
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Sine, 0.0) - 0.0f) < 1e-5f);
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Sine, 0.25) - 1.0f) < 1e-5f);
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Sine, 0.75) - (-1.0f)) < 1e-5f);

    // Square should be 1.0 for [0, 0.5) and -1.0 for [0.5, 1.0)
    CHECK(sample_normalized_waveform(WaveformType::Square, 0.1) == 1.0f);
    CHECK(sample_normalized_waveform(WaveformType::Square, 0.6) == -1.0f);

    // Triangle should be -1.0 at 0, 1.0 at 0.5, and -1.0 at 1.0
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Triangle, 0.0) - (-1.0f)) < 1e-5f);
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Triangle, 0.5) - 1.0f) < 1e-5f);

    // Sawtooth should be ramp from -1 to 1
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Sawtooth, 0.0) - (-1.0f)) < 1e-5f);
    CHECK(std::abs(sample_normalized_waveform(WaveformType::Sawtooth, 0.5) - 0.0f) < 1e-5f);
}

TEST_CASE("every waveform type produces a drawable frame") {
    DaqSettings s;
    s.amplitude_vpp = 4.0;
    for (auto type : {WaveformType::Sine, WaveformType::Square, WaveformType::Triangle,
                      WaveformType::Sawtooth, WaveformType::Noise}) {
        s.wave_type = type;
        auto t = synthesize_telemetry(s, 1.25);
        CHECK(t.scope_samples.size() == kScopeSamples);
        CHECK(t.v_per_div > 0.0);
        CHECK(t.measured_vpp > 0.0);
        CHECK(std::isfinite(t.measured_vrms));
        // The autoscale has to leave the trace on screen: 4 divisions above and below the centre.
        float peak = 0.0f;
        for (float v : t.scope_samples) peak = std::max(peak, std::abs(v));
        CHECK(static_cast<double>(peak) <= 4.0 * t.v_per_div + 1e-6);
    }
}

TEST_CASE("nice_step lands on a 1-2-5 step") {
    CHECK(nice_step(1.0) == doctest::Approx(1.0));
    CHECK(nice_step(1.4) == doctest::Approx(2.0));
    CHECK(nice_step(2.1) == doctest::Approx(5.0));
    CHECK(nice_step(0.7) == doctest::Approx(1.0));
    CHECK(nice_step(0.02) == doctest::Approx(0.02));
    CHECK(nice_step(0.0) == doctest::Approx(1.0));    // a flat trace still needs a scale
    CHECK(nice_step(-3.0) == doctest::Approx(1.0));
}

TEST_CASE("telemetry synthesis when output disabled") {
    DaqSettings s;
    s.output_enabled = false;
    s.target_voltage = 12.0;
    s.current_limit = 2.0;

    auto t = synthesize_telemetry(s, 1.0, kScopeWindowSeconds, 100);
    CHECK_FALSE(t.fault_tripped);
    CHECK(t.current_sense == 0.0);
    CHECK(t.power_watts == 0.0);
    CHECK(t.measured_vpp == 0.0);
    CHECK(t.scope_samples.size() == 100);
}

TEST_CASE("telemetry synthesis under normal load") {
    DaqSettings s;
    s.output_enabled = true;
    s.target_voltage = 10.0;
    s.current_limit = 3.0; // plenty of headroom for ~8 ohm load (needs ~1.25 A)
    s.amplitude_vpp = 4.0;
    s.frequency_hz = 500.0;
    s.offset_v = 1.0;
    s.wave_type = WaveformType::Sine;

    auto t = synthesize_telemetry(s, 0.5, kScopeWindowSeconds, 200);
    CHECK_FALSE(t.fault_tripped);
    CHECK_FALSE(t.constant_current);
    CHECK(t.voltage_sense > 9.0);
    CHECK(t.current_sense > 1.0);
    CHECK(t.current_sense < 2.0);
    CHECK(t.power_watts > 10.0);
    CHECK(t.measured_vpp > 3.0);
    CHECK(t.measured_vpp <= 4.2);
    CHECK(t.scope_samples.size() == 200);
    // A 2 ms sweep holds exactly one cycle at 500 Hz, which is below what counting crossings can
    // resolve: the readout falls back to the generator's setpoint rather than lying.
    CHECK(t.measured_freq == doctest::Approx(500.0));
}

TEST_CASE("the sine readouts are the ones the screen claims") {
    DaqSettings s;   // 5 Vpp sine, no offset
    auto t = synthesize_telemetry(s, 1.0);
    CHECK(t.measured_vpp == doctest::Approx(5.0).epsilon(0.02));
    CHECK(t.measured_vrms == doctest::Approx(2.5 / std::sqrt(2.0)).epsilon(0.02));
    CHECK(t.time_per_div == doctest::Approx(kScopeWindowSeconds / kScopeHDivisions));
}

TEST_CASE("measured frequency tracks the generator, not a hard-coded buffer") {
    DaqSettings s{.frequency_hz = 1000.0};
    CHECK(synthesize_telemetry(s, 1.0).measured_freq == doctest::Approx(1000.0).epsilon(0.01));

    s.frequency_hz = 10000.0;   // fills the same 2 ms sweep with 20 cycles instead of 2
    CHECK(synthesize_telemetry(s, 1.0).measured_freq == doctest::Approx(10000.0).epsilon(0.01));
}

TEST_CASE("the sweep is trigger-aligned: the trace stands still between frames") {
    DaqSettings s;   // 1 kHz sine
    auto a = synthesize_telemetry(s, 1.0);
    auto b = synthesize_telemetry(s, 1.0123);   // not a whole number of periods later
    for (std::size_t i : {50u, 100u, 250u})
        CHECK(a.scope_samples[i] == doctest::Approx(b.scope_samples[i]).epsilon(0.01));
}

TEST_CASE("degenerate frame sizes are safe") {
    DaqSettings s;
    auto empty = synthesize_telemetry(s, 1.0, kScopeWindowSeconds, 0);
    CHECK(empty.scope_samples.empty());
    CHECK(empty.v_per_div > 0.0);   // no division by zero in the drawing code

    auto single = synthesize_telemetry(s, 1.0, kScopeWindowSeconds, 1);
    CHECK(single.scope_samples.size() == 1);
    CHECK(single.v_per_div > 0.0);
}

TEST_CASE("telemetry synthesis with current limit compliance") {
    DaqSettings s;
    s.output_enabled = true;
    s.target_voltage = 20.0;
    s.current_limit = 0.5; // low limit clamps current

    auto t = synthesize_telemetry(s, 0.2, kScopeWindowSeconds, 100);
    CHECK_FALSE(t.fault_tripped);
    CHECK(t.constant_current);
    CHECK(t.current_sense == doctest::Approx(0.5).epsilon(0.01));
    CHECK(t.voltage_sense < 20.0); // clamped down by current compliance
}

TEST_CASE("telemetry synthesis on fault") {
    DaqSettings s;
    s.simulate_fault = true;

    auto t = synthesize_telemetry(s, 1.5, kScopeWindowSeconds, 100);
    CHECK(t.fault_tripped);
    CHECK(t.voltage_sense == 0.0);
    CHECK(t.power_watts == 0.0);
    CHECK(t.temperature_c >= 60.0);
}
