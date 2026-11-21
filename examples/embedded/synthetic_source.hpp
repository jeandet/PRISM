#pragma once

// A generated test signal for where there is no ADC input (Renode, host): a sine sweeping
// 200 Hz -> 4 kHz over 5 s, repeating, amplitude 0.5, plus a little noise. It produces as
// many samples as real time has elapsed, like the ADC would.

#include <embedded/sample_source.hpp>

#include <chrono>
#include <cstdint>

namespace prism_examples {

class SyntheticSource final : public SampleSource {
public:
    explicit SyntheticSource(float sample_rate);

    std::size_t read(std::span<float> out) override;
    float sample_rate() const override { return rate_; }
    const char *name() const override { return "synthetic"; }

    // The signal itself, a pure function of time: out[i] is the sample at t + i / rate.
    void read_at(std::span<float> out, double t_seconds);

private:
    float rate_;
    double t_ = 0.0; // the time of the next sample read() returns
    std::chrono::steady_clock::time_point last_;
    std::uint32_t noise_state_ = 1;
};

} // namespace prism_examples
