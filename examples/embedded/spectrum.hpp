#pragma once

// Magnitude spectrum of one block of samples: Hann window, real FFT (CMSIS-DSP
// arm_rfft_fast_f32), magnitudes scaled so a full-scale sine reads 1 (0 dB).

#include "arm_math.h"

#include <cstddef>
#include <span>
#include <vector>

namespace prism_examples {

class Spectrum {
public:
    explicit Spectrum(std::size_t fft_size); // a power of two from 32 to 4096

    // in.size() == fft_size; out.size() == fft_size / 2 (bins 0 .. Nyquist excluded).
    void compute_linear(std::span<const float> in, std::span<float> out);
    // As compute_linear, in dB, floored at kFloorDb.
    void compute(std::span<const float> in, std::span<float> out_db);

    static constexpr float kFloorDb = -100.f;

private:
    arm_rfft_fast_instance_f32 fft_{};
    std::vector<float> window_, work_, bins_;
    float scale_ = 0.f; // 2 / sum(window): a full-scale sine's peak bin becomes 1
};

} // namespace prism_examples
