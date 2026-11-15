#pragma once

#include <algorithm>

// The gate envelope: linear attack and release, landing on exactly zero rather than nearly zero,
// so a released voice is silent rather than a -80 dB drone.
namespace prism::synth {

class Envelope {
public:
    void reset() noexcept {
        level_ = 0.f;
        gate_ = false;
    }

    void set_times(double sample_rate, double attack_ms, double release_ms) noexcept {
        attack_step_ = static_cast<float>(1.0 / std::max(1.0, attack_ms * 1e-3 * sample_rate));
        release_step_ = static_cast<float>(1.0 / std::max(1.0, release_ms * 1e-3 * sample_rate));
    }

    void gate(bool open) noexcept { gate_ = open; }

    float tick() noexcept {
        if (gate_) {
            level_ = std::min(1.f, level_ + attack_step_);
            return level_;
        }
        level_ -= release_step_;
        if (level_ < 0.f) level_ = 0.f;
        return level_;
    }

    float level() const noexcept { return level_; }

private:
    float level_ = 0.f;
    float attack_step_ = 0.01f;
    float release_step_ = 0.001f;
    bool gate_ = false;
};

} // namespace prism::synth
