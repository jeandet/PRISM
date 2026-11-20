#pragma once

#include "settings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

// What the analyzer panel draws, computed from the samples the audio callback published. Pure
// like the model: no threads, no IO. tests/test_synth_rack.cpp checks the FFT and the ring.
namespace prism::synth {

inline constexpr std::size_t kFftSize = 2048;
inline constexpr std::size_t kBandCount = 72;
inline constexpr std::size_t kScopeSamples = 960;   // 20 ms at 48 kHz
inline constexpr float kFloorDb = -90.f;

// One display frame: fixed size, trivially copyable, comparable -- it travels between threads
// and a repaint happens when a new one differs from the one on screen.
struct AnalyzerFrame {
    std::array<float, kBandCount> spectrum_db{};
    std::array<float, kScopeSamples> scope{};
    float peak_db = kFloorDb;
    float rms_db = kFloorDb;
    std::size_t step = 0;
    int note = -1;
    bool sounding = false;
    float callback_max_ms = 0.f;
    float callback_mean_ms = 0.f;
    std::uint32_t buffer_frames = 0;
    std::uint32_t parameter_updates = 0;
    double sample_rate = kSampleRate;

    bool operator==(const AnalyzerFrame&) const = default;
};

// Iterative radix-2 FFT, twiddles precomputed once, in place on two float arrays.
template <std::size_t N>
class Fft {
    static_assert(std::has_single_bit(N), "N must be a power of two");

public:
    Fft() {
        for (std::size_t i = 0; i < N / 2; ++i) {
            const double angle = -2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(N);
            cos_[i] = static_cast<float>(std::cos(angle));
            sin_[i] = static_cast<float>(std::sin(angle));
        }
    }

    void forward(float* re, float* im) const noexcept {
        bit_reverse(re, im);
        for (std::size_t len = 2; len <= N; len <<= 1) {
            const std::size_t half = len >> 1;
            const std::size_t stride = N / len;
            for (std::size_t base = 0; base < N; base += len)
                for (std::size_t k = 0; k < half; ++k) {
                    const float wr = cos_[k * stride];
                    const float wi = sin_[k * stride];
                    const std::size_t a = base + k;
                    const std::size_t b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
        }
    }

private:
    void bit_reverse(float* re, float* im) const noexcept {
        for (std::size_t i = 1, j = 0; i < N; ++i) {
            std::size_t bit = N >> 1;
            for (; (j & bit) != 0u; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
    }

    std::array<float, N / 2> cos_{};
    std::array<float, N / 2> sin_{};
};

// Log-spaced bands from 20 Hz to 20 kHz: each band covers a range of FFT bins, so the display
// has the same width per octave rather than per hertz (where all the music would be in the
// first few pixels).
class SpectrumMap {
public:
    static constexpr double kLowHz = 20.0;
    static constexpr double kHighHz = 20000.0;

    SpectrumMap() = default;

    explicit SpectrumMap(double sample_rate, std::size_t fft_size = kFftSize) {
        const double bin_hz = sample_rate / static_cast<double>(fft_size);
        for (std::size_t band = 0; band < kBandCount; ++band) {
            const auto low = to_bin(band_hz(band), bin_hz, fft_size);
            bin_low_[band] = low;
            bin_high_[band] = std::max(low, to_bin(band_hz(band + 1), bin_hz, fft_size));
        }
    }

    std::size_t bin_low(std::size_t band) const { return bin_low_[band]; }
    std::size_t bin_high(std::size_t band) const { return bin_high_[band]; }

    // A band's lower edge, for ruler labels.
    static double band_hz(std::size_t band) {
        const double ratio = static_cast<double>(band) / static_cast<double>(kBandCount);
        return kLowHz * std::pow(kHighHz / kLowHz, ratio);
    }

private:
    static std::size_t to_bin(double hz, double bin_hz, std::size_t fft_size) {
        const double bin = std::round(hz / bin_hz);
        return std::clamp(static_cast<std::size_t>(bin), std::size_t{1}, fft_size / 2 - 1);
    }

    std::array<std::size_t, kBandCount> bin_low_{};
    std::array<std::size_t, kBandCount> bin_high_{};
};

// Index of the newest rising zero crossing that still leaves a full window after it, or the
// newest possible start when the signal has none. Triggering on the signal's own edge is what
// holds a steady note still on the scope instead of letting it crawl.
inline std::size_t trigger_start(std::span<const float> samples, std::size_t window) {
    if (samples.size() <= window) return 0;
    const std::size_t limit = samples.size() - window;
    for (std::size_t i = limit + 1; i-- > 1;)
        if (samples[i - 1] <= 0.f && samples[i] > 0.f) return i;
    return limit;
}

// One FFT of the newest kFftSize samples folded into the display bands, plus a triggered scope
// window and a peak/RMS reading. Its buffers are reused across frames: nothing is allocated per
// call, so it can run as often as the UI likes.
class SpectrumAnalyzer {
public:
    explicit SpectrumAnalyzer(double sample_rate = kSampleRate)
        : map_(sample_rate, kFftSize), sample_rate_(sample_rate) {
        for (std::size_t i = 0; i < kFftSize; ++i) {
            const double phase = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(kFftSize);
            window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(phase));
        }
        held_.fill(kFloorDb);
    }

    AnalyzerFrame analyze(std::span<const float> newest) {
        AnalyzerFrame frame{};
        frame.sample_rate = sample_rate_;
        if (newest.size() < 2) {
            frame.spectrum_db = held_;
            return frame;
        }
        fill_bands(frame, newest);
        frame.peak_db = db_of(peak(newest));
        frame.rms_db = db_of(rms(newest));
        frame.scope = scope_window(newest);
        return frame;
    }

private:
    // Full scale is a unit-amplitude sine in one bin: the Hann window halves it and the DFT
    // of N real samples halves it again, so |X| of a full-scale tone is N/4.
    static constexpr float kFullScaleMagnitude = static_cast<float>(kFftSize) * 0.25f;
    static constexpr float kDecayDb = 1.6f;   // per frame: bars fall, they do not flicker

    static float db_of(float magnitude) {
        return std::max(kFloorDb, 20.f * std::log10(std::max(magnitude, 1e-9f)));
    }

    static float peak(std::span<const float> samples) {
        float out = 0.f;
        for (float sample : samples) out = std::max(out, std::abs(sample));
        return out;
    }

    static float rms(std::span<const float> samples) {
        float sum = 0.f;
        for (float sample : samples) sum += sample * sample;
        return std::sqrt(sum / static_cast<float>(samples.size()));
    }

    std::array<float, kScopeSamples> scope_window(std::span<const float> newest) const {
        std::array<float, kScopeSamples> out{};
        const std::size_t count = std::min(newest.size(), kScopeSamples);
        std::ranges::copy(newest.subspan(trigger_start(newest, kScopeSamples), count),
                          out.end() - static_cast<std::ptrdiff_t>(count));
        return out;
    }

    void fill_bands(AnalyzerFrame& frame, std::span<const float> newest) {
        const std::size_t count = std::min(newest.size(), kFftSize);
        re_.fill(0.f);
        im_.fill(0.f);
        const std::size_t offset = newest.size() - count;
        for (std::size_t i = 0; i < count; ++i) re_[i] = newest[offset + i] * window_[i];

        fft_.forward(re_.data(), im_.data());

        for (std::size_t band = 0; band < kBandCount; ++band) {
            float magnitude = 0.f;
            for (std::size_t bin = map_.bin_low(band); bin <= map_.bin_high(band); ++bin)
                magnitude = std::max(magnitude, std::hypot(re_[bin], im_[bin]));
            const float db = db_of(magnitude / kFullScaleMagnitude);
            held_[band] = std::max(db, held_[band] - kDecayDb);
            frame.spectrum_db[band] = held_[band];
        }
    }

    SpectrumMap map_{};
    Fft<kFftSize> fft_{};
    std::array<float, kFftSize> window_{};
    std::array<float, kFftSize> re_{};
    std::array<float, kFftSize> im_{};
    std::array<float, kBandCount> held_{};
    double sample_rate_ = kSampleRate;
};

} // namespace prism::synth
