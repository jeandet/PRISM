#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/raster/canvas.hpp>
#include <prism/raster/fonts.hpp>

#include <prism/core/utf8.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

using namespace prism::core;
using prism::render::FontStyle;
using prism::render::TextAnchor;

namespace {
constexpr std::uint32_t kCleared = 0xFF000000u;   // opaque black
} // namespace

TEST_CASE("width(\"Hello\") is the sum of advances plus kerning") {
    prism::raster::Fonts fonts;
    const std::string s = "Hello";   // ASCII: one code point per byte
    const float size = 16.f;
    const float w = fonts.width(s, size, FontStyle{});
    CHECK(w > 0.f);
    float expect = 0.f;
    std::uint32_t prev = 0;
    for (char ch : s) {
        const std::uint32_t cp = static_cast<unsigned char>(ch);
        if (prev != 0)
            expect += fonts.kern(prism::raster::Fonts::kRegular, prev, cp, size);
        expect += fonts.advance(prism::raster::Fonts::kRegular, cp, size);
        prev = cp;
    }
    CHECK(w == doctest::Approx(expect));
    // and it is not the fixed-width estimate: proportional glyphs differ per letter
    CHECK(fonts.advance(prism::raster::Fonts::kRegular, 'i', size)
          != doctest::Approx(fonts.advance(prism::raster::Fonts::kRegular, 'W', size)));
}

TEST_CASE("line_height is positive and grows with size") {
    prism::raster::Fonts fonts;
    CHECK(fonts.line_height(16.f, FontStyle{}) > 16.f);
    CHECK(fonts.line_height(32.f, FontStyle{}) > fonts.line_height(16.f, FontStyle{}));
}

TEST_CASE("drawing \"Hello\" at size 16 puts coverage only inside its measured box (+2 px)") {
    prism::raster::Fonts fonts;
    const std::string s = "Hello";
    const float size = 16.f;
    const float w = fonts.width(s, size, FontStyle{});
    const float h = fonts.line_height(size, FontStyle{});
    constexpr int W = 80, H = 40;
    std::array<std::uint32_t, W * H> px{};
    prism::raster::Canvas canvas{px, W, H, W};
    canvas.clear(Color::rgba(0, 0, 0));
    const float ox = 4.f, oy = 4.f;
    canvas.text(fonts, s, Point{X{ox}, Y{oy}}, size, Color::rgba(255, 255, 255), 0.f,
                TextAnchor::TopLeft, FontStyle{});

    int min_x = W, max_x = -1, min_y = H, max_y = -1, ink = 0, inside = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (px[static_cast<std::size_t>(y) * W + x] == kCleared) continue;
            ++ink;
            min_x = std::min(min_x, x);  max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);  max_y = std::max(max_y, y);
            // pixel centre inside the exact measured box
            if (x + 0.5f >= ox && x + 0.5f < ox + w && y + 0.5f >= oy && y + 0.5f < oy + h) ++inside;
        }
    CHECK(ink > 0);       // some coverage inside
    CHECK(inside > 0);
    // all coverage within the box grown by 2 px (glyph bitmaps may overhang the advance slightly)
    CHECK(min_x >= static_cast<int>(std::floor(ox)) - 2);
    CHECK(max_x <= static_cast<int>(std::ceil(ox + w)) + 2);
    CHECK(min_y >= static_cast<int>(std::floor(oy)) - 2);
    CHECK(max_y <= static_cast<int>(std::ceil(oy + h)) + 2);
}

TEST_CASE("text is alpha-blended, not stamped opaque") {
    prism::raster::Fonts fonts;
    constexpr int W = 40, H = 24;
    std::array<std::uint32_t, W * H> px{};
    prism::raster::Canvas canvas{px, W, H, W};
    canvas.clear(Color::rgba(255, 255, 255));   // white
    canvas.text(fonts, "H", Point{X{2}, Y{2}}, 16.f, Color::rgba(0, 0, 0, 128), 0.f,
                TextAnchor::TopLeft, FontStyle{});
    // glyph edges are partial coverage: some pixel is grey, not pure black or pure white
    bool grey = false, black = false;
    for (std::uint32_t p : px) {
        if (p != 0xFFFFFFFFu && p != 0xFF000000u) grey = true;
        if (p == 0xFF000000u) black = true;
    }
    CHECK(grey);   // blended edges
    (void)black;
}

TEST_CASE("a UTF-8 string measures per code point, not per byte") {
    prism::raster::Fonts fonts;
    const std::string s = "é";   // U+00E9, 2 bytes in UTF-8
    CHECK(prism::core::utf8::count(s) == 1);
    const float w = fonts.width(s, 16.f, FontStyle{});
    CHECK(w > 0.f);
    CHECK(w == doctest::Approx(fonts.advance(prism::raster::Fonts::kRegular, 0xE9, 16.f)));
}
