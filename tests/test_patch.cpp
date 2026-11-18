// The gallery's Nodes tab: its signal blocks are plain functions over sample vectors, tested
// here with no window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/patch.hpp"

#include <algorithm>
#include <cmath>

using namespace patch;

TEST_CASE("a sine has the asked amplitude and its cycles across the window") {
    auto s = sine(2.0, 0.5, 0.0);
    REQUIRE(s.size() == samples);
    CHECK(s[0] == doctest::Approx(0.0));
    CHECK(*std::ranges::max_element(s) == doctest::Approx(0.5).epsilon(0.01));
    CHECK(s[samples / 8] == doctest::Approx(0.5).epsilon(0.01));   // a quarter of the first cycle
}

TEST_CASE("noise stays within its level and is the same at every call") {
    auto n = noise(0.3);
    REQUIRE(n.size() == samples);
    CHECK(std::ranges::all_of(n, [](double v) { return std::abs(v) <= 0.3; }));
    CHECK(n == noise(0.3));
}

TEST_CASE("a mix scales each input; a missing input counts as silence") {
    std::vector<double> a(samples, 1.0), b(samples, 2.0);
    auto m = mix(a, b, 0.5, 0.25);
    REQUIRE(m.size() == samples);
    CHECK(m[3] == doctest::Approx(1.0));
    auto only_a = mix(a, {}, 2.0, 1.0);
    REQUIRE(only_a.size() == samples);
    CHECK(only_a[7] == doctest::Approx(2.0));
}

TEST_CASE("a low-pass smooths a step: full cutoff passes it, a low one slows it") {
    std::vector<double> step(samples, 1.0);
    step[0] = 0.0;
    CHECK(low_pass(step, 1.0)[1] == doctest::Approx(1.0));
    auto slow = low_pass(step, 0.1);
    CHECK(slow[1] < 0.2);
    CHECK(slow.back() == doctest::Approx(1.0).epsilon(0.01));
    CHECK(low_pass({}, 0.5).empty());
}

TEST_CASE("a square is ±its amplitude, high where a sine of it is not negative") {
    auto s = square(2.0, 0.4, 0.0);
    REQUIRE(s.size() == samples);
    CHECK(std::ranges::all_of(s, [](double v) { return std::abs(std::abs(v) - 0.4) < 1e-12; }));
    CHECK(s[0] == doctest::Approx(0.4));   // it starts high, as a sine starts at zero
    auto sines = sine(2.0, 0.4, 0.0);
    CHECK(std::ranges::equal(s, sines, [](double sq, double sin) { return (sq < 0) == (sin < 0); }));
}

TEST_CASE("a one-cycle square is high for its first half, low for its second") {
    auto s = square(1.0, 1.0, 0.0);
    REQUIRE(s.size() == samples);
    CHECK(s[0] == 1.0);
    CHECK(s[samples / 2 - 1] == 1.0);
    CHECK(s[samples / 2 + 1] == -1.0);
    CHECK(s[samples - 1] == -1.0);
    CHECK(std::ranges::all_of(square(1.0, 0.0, 0.0), [](double v) { return v == 0.0; }));
}

TEST_CASE("a square's phase moves its edges, as a sine's does") {
    CHECK(square(1.0, 1.0, 0.25)[0] == 1.0);
    CHECK(square(1.0, 1.0, 0.75)[0] == -1.0);
    CHECK(square(1.0, 1.0, 0.5)[samples / 4] == -1.0);   // half a turn in
}

TEST_CASE("a gain scales every sample, and an unwired wave stays silent") {
    std::vector<double> w(samples, 2.0);
    auto halved = gain(w, 0.5);
    REQUIRE(halved.size() == samples);
    CHECK(halved[0] == doctest::Approx(1.0));
    CHECK(halved[samples - 1] == doctest::Approx(1.0));
    CHECK(gain({}, 3.0).empty());
    auto quiet = gain(sine(1.0, 1.0, 0.0), 0.25);
    REQUIRE(quiet.size() == samples);
    CHECK(*std::ranges::max_element(quiet) == doctest::Approx(0.25).epsilon(0.01));
}
