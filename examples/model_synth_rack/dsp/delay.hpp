#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// A feedback delay with a fractional read head, so turning the time knob glides instead of
// clicking. Fixed storage: the longest time it holds is kCapacity / sample rate.
namespace prism::synth {

class DelayLine {
public:
    static constexpr std::size_t kCapacity = 1u << 15;   // 0.68 s at 48 kHz

    void reset() noexcept { buffer_.fill(0.f); }

    void set_time(double sample_rate, double ms) noexcept {
        delay_ = std::clamp(ms * 1e-3 * sample_rate, 1.0, static_cast<double>(kCapacity - 2));
    }

    float process(float x, double feedback, double mix) noexcept {
        const float wet = read_tap();
        buffer_[write_] = x + wet * static_cast<float>(feedback);
        write_ = (write_ + 1) % kCapacity;
        return x + (wet - x) * static_cast<float>(mix);
    }

private:
    float read_tap() const noexcept {
        double pos = static_cast<double>(write_) - delay_;
        while (pos < 0.0) pos += static_cast<double>(kCapacity);
        const auto first = static_cast<std::size_t>(pos);
        const auto second = (first + 1) % kCapacity;
        const float frac = static_cast<float>(pos - static_cast<double>(first));
        return buffer_[first] + (buffer_[second] - buffer_[first]) * frac;
    }

    std::array<float, kCapacity> buffer_{};
    std::size_t write_ = 0;
    double delay_ = 4800.0;
};

} // namespace prism::synth
