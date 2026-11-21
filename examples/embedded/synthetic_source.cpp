#include <embedded/synthetic_source.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism_examples {
namespace {

constexpr double kStartHz = 200.0, kEndHz = 4000.0, kSweepSeconds = 5.0;
constexpr float kAmplitude = 0.5f, kNoise = 0.01f;

// Phase of a linear sweep, restarting every kSweepSeconds.
double sweep_phase(double t)
{
    const double u = std::fmod(t, kSweepSeconds);
    const double rate = (kEndHz - kStartHz) / kSweepSeconds;
    return 2.0 * std::numbers::pi * (kStartHz * u + 0.5 * rate * u * u);
}

float next_noise(std::uint32_t &state) // xorshift32, in [-1, 1]
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state) / float(UINT32_MAX) * 2.f - 1.f;
}

} // namespace

SyntheticSource::SyntheticSource(float sample_rate)
    : rate_(sample_rate), last_(std::chrono::steady_clock::now())
{
}

void SyntheticSource::read_at(std::span<float> out, double t_seconds)
{
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = t_seconds + double(i) / rate_;
        out[i] = kAmplitude * float(std::sin(sweep_phase(t))) + kNoise * next_noise(noise_state_);
    }
}

std::size_t SyntheticSource::read(std::span<float> out)
{
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::min(std::chrono::duration<double>(now - last_).count(), 1.0);
    const auto n = std::min(out.size(), std::size_t(elapsed * rate_));
    last_ += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(double(n) / rate_));
    if (elapsed >= 1.0)
        last_ = now; // simplify: a stall longer than 1 s drops the backlog instead of replaying it
    read_at(out.first(n), t_);
    t_ += double(n) / rate_;
    return n;
}

} // namespace prism_examples
