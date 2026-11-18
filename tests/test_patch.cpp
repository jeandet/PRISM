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
