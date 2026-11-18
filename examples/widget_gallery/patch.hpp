// The Nodes tab's signal blocks: plain functions over a fixed number of samples, one window of
// signal. Tested in tests/test_patch.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

namespace patch {

inline constexpr int samples = 240;
using Wave = std::vector<double>;

// `cycles` whole periods across the window, starting `phase` turns in.
inline Wave sine(double cycles, double amplitude, double phase) {
    Wave w(samples);
    for (int i = 0; i < samples; ++i)
        w[static_cast<std::size_t>(i)] = amplitude * std::sin(2 * std::numbers::pi * (cycles * i / samples + phase));
    return w;
}

// The same noise at every call (a fixed seed): moving the level only scales it.
inline Wave noise(double level) {
    std::mt19937 gen(7);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    Wave w(samples);
    for (double& v : w) v = level * d(gen);
    return w;
}

// An unwired input is an empty wave: it counts as silence.
inline Wave mix(const Wave& a, const Wave& b, double gain_a, double gain_b) {
    auto at = [](const Wave& w, std::size_t i) { return i < w.size() ? w[i] : 0.0; };
    Wave w(std::max(a.size(), b.size()));
    for (std::size_t i = 0; i < w.size(); ++i) w[i] = gain_a * at(a, i) + gain_b * at(b, i);
    return w;
}

// One-pole smoothing: each sample moves `cutoff` (0..1) of the way to the input.
inline Wave low_pass(const Wave& in, double cutoff) {
    Wave w(in.size());
    double y = in.empty() ? 0.0 : in.front();
    for (std::size_t i = 0; i < in.size(); ++i) w[i] = y += cutoff * (in[i] - y);
    return w;
}

} // namespace patch
