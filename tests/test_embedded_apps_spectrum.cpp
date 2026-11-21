#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <embedded/spectrum.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace {
constexpr float kRate = 16000.f;
constexpr std::size_t kSize = 1024;

std::vector<float> sine(float freq, float amplitude = 1.f)
{
    std::vector<float> v(kSize);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = amplitude * std::sin(2.f * std::numbers::pi_v<float> * freq * float(i) / kRate);
    return v;
}

std::ptrdiff_t peak_bin(const std::vector<float> &v)
{
    return std::distance(v.begin(), std::ranges::max_element(v));
}
} // namespace

TEST_CASE("spectrum: a full-scale 1 kHz sine peaks in bin 64 at about 0 dB")
{
    prism_examples::Spectrum s{kSize};
    std::vector<float> db(kSize / 2);
    s.compute(sine(1000.f), db); // 1000 / (16000 / 1024) = 64
    CHECK(peak_bin(db) == 64);
    CHECK(std::abs(db[64]) < 0.5f);
}

TEST_CASE("spectrum: silence stays finite at the -100 dB floor")
{
    prism_examples::Spectrum s{kSize};
    std::vector<float> in(kSize, 0.f), db(kSize / 2);
    s.compute(in, db);
    CHECK(std::ranges::all_of(db, [](float v) { return std::isfinite(v) && v == -100.f; }));
}

TEST_CASE("spectrum: linear magnitude of a full-scale sine peaks near 1")
{
    prism_examples::Spectrum s{kSize};
    std::vector<float> mag(kSize / 2);
    s.compute_linear(sine(1000.f), mag);
    CHECK(peak_bin(mag) == 64);
    CHECK(mag[64] == doctest::Approx(1.f).epsilon(0.05));
}
