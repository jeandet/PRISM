#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <embedded/spectrum.hpp>
#include <embedded/synthetic_source.hpp>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace {
constexpr float kRate = 16000.f;

float dominant_hz(prism_examples::SyntheticSource &src, double t_seconds)
{
    std::vector<float> block(1024), mag(512);
    src.read_at(block, t_seconds);
    prism_examples::Spectrum{1024}.compute_linear(block, mag);
    const auto bin = std::distance(mag.begin(), std::ranges::max_element(mag));
    return float(bin) * kRate / 1024.f;
}
} // namespace

TEST_CASE("synthetic source: reads only what elapsed, values in [-1, 1]")
{
    prism_examples::SyntheticSource src{kRate};
    std::vector<float> out(16000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto n = src.read(out);
    CHECK(n >= 700);   // ~800 samples in 50 ms
    CHECK(n < out.size());
    CHECK(std::all_of(out.begin(), out.begin() + std::ptrdiff_t(n),
                      [](float v) { return v >= -1.f && v <= 1.f; }));
    CHECK(src.sample_rate() == kRate);
}

TEST_CASE("synthetic source: the dominant frequency rises over the sweep")
{
    prism_examples::SyntheticSource src{kRate};
    const float early = dominant_hz(src, 0.5);
    const float late = dominant_hz(src, 3.5);
    CHECK(early < 1000.f);
    CHECK(late > 2000.f);
}
