#pragma once

#include "settings.hpp"

#include <cmath>
#include <numbers>

// The tone source: a phase accumulator with polyBLEP on the discontinuous waveforms, so the saw
// and the square do not alias. Non-deprecated alias-free waveforms are not the point here.
// simplify: sine and triangle stay naive (their harmonics fall off fast enough to be inaudible
// here). Upgrade path: polyBLAMP, integrating the square's BLEP twice.
namespace prism::synth {

class Oscillator {
public:
    void reset() noexcept { phase_ = 0.0; }
    void set_increment(double increment) noexcept { increment_ = increment; }

    float tick(Waveform waveform) noexcept {
        const double t = phase_;
        float sample = 0.f;
        switch (waveform) {
            case Waveform::Sine:
                sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * t));
                break;
            case Waveform::Saw:
                sample = static_cast<float>(2.0 * t - 1.0 - poly_blep(t, increment_));
                break;
            case Waveform::Square: {
                const double raw = t < 0.5 ? 1.0 : -1.0;
                const double wrapped = t + 0.5 >= 1.0 ? t - 0.5 : t + 0.5;
                sample = static_cast<float>(raw + poly_blep(t, increment_)
                                            - poly_blep(wrapped, increment_));
                break;
            }
            case Waveform::Triangle:
                sample = static_cast<float>(1.0 - 4.0 * std::abs(t - 0.5));
                break;
        }
        phase_ += increment_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return sample;
    }

private:
    static double poly_blep(double t, double dt) noexcept {
        if (dt <= 0.0) return 0.0;
        if (t < dt) {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt) {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    double phase_ = 0.0;
    double increment_ = 0.0;
};

} // namespace prism::synth
