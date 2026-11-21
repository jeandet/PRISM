#include <embedded/spectrum.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace prism_examples {
namespace {

std::vector<float> hann(std::size_t n)
{
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = 0.5f - 0.5f * std::cos(2.f * std::numbers::pi_v<float> * float(i) / float(n));
    return w;
}

} // namespace

Spectrum::Spectrum(std::size_t fft_size)
    : window_(hann(fft_size)), work_(fft_size), bins_(fft_size),
      scale_(2.f / std::accumulate(window_.begin(), window_.end(), 0.f))
{
    arm_rfft_fast_init_f32(&fft_, static_cast<uint16_t>(fft_size));
}

void Spectrum::compute_linear(std::span<const float> in, std::span<float> out)
{
    std::ranges::transform(in, window_, work_.begin(), std::multiplies{});
    arm_rfft_fast_f32(&fft_, work_.data(), bins_.data(), 0);
    // bins_ = [re0, re(N/2), re1, im1, re2, im2, ...]: bin 0 is real (its slot 1 holds Nyquist).
    out[0] = std::abs(bins_[0]) * scale_ / 2.f;
    for (std::size_t k = 1; k < out.size(); ++k)
        out[k] = std::hypot(bins_[2 * k], bins_[2 * k + 1]) * scale_;
}

void Spectrum::compute(std::span<const float> in, std::span<float> out_db)
{
    compute_linear(in, out_db);
    for (float &v : out_db)
        v = v > 0.f ? std::max(20.f * std::log10(v), kFloorDb) : kFloorDb;
}

} // namespace prism_examples
