// Image files decoded into Image (render/image_decoder.hpp), by the SDL3_image implementation.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/image_decoder.hpp>

#include <array>
#include <cstddef>
#include <string_view>

using namespace prism;
using namespace prism::core;
using namespace prism::render;

namespace {
// A 2x1 PNG: opaque red, then blue at half alpha.
constexpr std::array<unsigned char, 73> tiny_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f,
    0x8a, 0x00, 0x00, 0x00, 0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x42, 0x0d, 0x00, 0x0f, 0x7a, 0x03, 0x7e, 0x77, 0xe9, 0x7f, 0x97, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

std::span<const std::byte> bytes_of(std::string_view s) { return std::as_bytes(std::span{s.data(), s.size()}); }
}

TEST_CASE("a PNG decodes to its size and straight-alpha RGBA pixels") {
    auto img = decode_image(std::as_bytes(std::span{tiny_png}));
    REQUIRE(img);
    CHECK((*img)->width() == 2);
    CHECK((*img)->height() == 1);
    auto px = (*img)->pixels();
    REQUIRE(px.size() == 8);
    CHECK(px[0] == 255); CHECK(px[1] == 0); CHECK(px[2] == 0); CHECK(px[3] == 255);
    CHECK(px[4] == 0);   CHECK(px[5] == 0); CHECK(px[6] == 255); CHECK(px[7] == 128);
}

TEST_CASE("an SVG is drawn as large as fits the box asked for, keeping its aspect ratio") {
    constexpr std::string_view svg =
        R"(<svg xmlns="http://www.w3.org/2000/svg" width="10" height="10" viewBox="0 0 10 10">)"
        R"(<rect width="10" height="10" fill="#00ff00"/></svg>)";
    auto img = decode_image(bytes_of(svg), Size{Width{32}, Height{24}});
    REQUIRE(img);
    CHECK((*img)->width() == 24);    // a square image in a 32 x 24 box
    CHECK((*img)->height() == 24);
    auto px = (*img)->pixels();
    std::size_t mid = (12 * 24 + 12) * 4;
    CHECK(px[mid + 1] == 255);   // green
    CHECK(px[mid + 3] == 255);   // opaque
}

TEST_CASE("bytes that are no image give an error, not a crash") {
    auto img = decode_image(bytes_of("certainly not an image"));
    CHECK_FALSE(img);
    CHECK_FALSE(img.error().empty());
}

TEST_CASE("a missing file gives an error") {
    auto img = load_image("/nonexistent/prism/icon.png");
    CHECK_FALSE(img);
}
