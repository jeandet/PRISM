#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

// The resonant low-pass: topology-preserving state-variable (Zavalishin). The coefficients are
// prewarped, so sweeping the cutoff stays stable, and resonance is the filter's Q rather than a
// feedback gain.
namespace prism::synth {

class SvfLowpass {
public:
    void reset() noexcept {
        ic1_ = 0.0;
        ic2_ = 0.0;
    }

    void set(double sample_rate, double cutoff_hz, double q) noexcept {
        const double fc = std::clamp(cutoff_hz, 20.0, sample_rate * 0.5 * 0.95);
        const double g = std::tan(std::numbers::pi * fc / sample_rate);
        const double k = 1.0 / std::clamp(q, 0.5, 20.0);
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    float process(float x) noexcept {
        const double v3 = static_cast<double>(x) - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return static_cast<float>(v2);
    }

private:
    double a1_ = 1.0;
    double a2_ = 0.0;
    double a3_ = 0.0;
    double ic1_ = 0.0;
    double ic2_ = 0.0;
};

} // namespace prism::synth
