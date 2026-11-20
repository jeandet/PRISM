#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/dev/spsc_ring.hpp>

#include <array>
#include <vector>

TEST_CASE("spsc ring: what is pushed is read back in order")
{
    prism::dev::SpscRing<float, 8> ring;
    const std::array<float, 3> in{1.f, 2.f, 3.f};
    ring.push(in);
    std::array<float, 8> out{};
    CHECK(ring.pop(out) == 3);
    CHECK(out[0] == 1.f);
    CHECK(out[2] == 3.f);
    CHECK(ring.pop(out) == 0);
}

TEST_CASE("spsc ring: wraps around its end")
{
    prism::dev::SpscRing<float, 8> ring;
    std::array<float, 8> out{};
    for (int round = 0; round < 5; ++round) {
        const std::array<float, 5> in{float(round), 1.f, 2.f, 3.f, float(round)};
        ring.push(in);
        REQUIRE(ring.pop(out) == 5);
        CHECK(out[0] == float(round));
        CHECK(out[4] == float(round));
    }
}

TEST_CASE("spsc ring: when full, new samples are dropped and counted")
{
    prism::dev::SpscRing<float, 8> ring;
    std::vector<float> in(10, 7.f);
    ring.push(in);
    CHECK(ring.dropped() == 3); // holds capacity - 1 = 7
    std::array<float, 16> out{};
    CHECK(ring.pop(out) == 7);
}
