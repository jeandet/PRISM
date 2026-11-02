#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_font_set.hpp>

#include <SDL3_ttf/SDL_ttf.h>

using prism::render::FontRole;

namespace {
struct TtfSession {
    TtfSession() { TTF_Init(); }
    ~TtfSession() { TTF_Quit(); }
};
}

TEST_CASE("each role gets its own font, stable across size changes") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    TTF_Font* ui = fonts.get(FontRole::Ui, 14.f);
    TTF_Font* mono = fonts.get(FontRole::Mono, 14.f);
    CHECK(ui != nullptr);
    CHECK(mono != nullptr);
    CHECK(ui != mono);
    CHECK(fonts.get(FontRole::Ui, 28.f) == ui);
    CHECK(TTF_GetFontSize(mono) == doctest::Approx(28.f));   // the fallback was resized too
}

TEST_CASE("one missing font: the other serves both roles") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, nullptr, 14.f));
    CHECK(fonts.get(FontRole::Mono, 14.f) == fonts.get(FontRole::Ui, 14.f));
}

TEST_CASE("get before open is null; open and close twice are safe") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    CHECK(fonts.get(FontRole::Ui, 14.f) == nullptr);
    CHECK(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    CHECK(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    fonts.close();
    fonts.close();
    CHECK(fonts.get(FontRole::Ui, 14.f) == nullptr);
    CHECK_FALSE(fonts.open("/nonexistent/a.ttf", "/nonexistent/b.ttf", 14.f));
}

TEST_CASE("the same file for both roles is opened once") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_MONO_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    CHECK(fonts.get(FontRole::Ui, 14.f) == fonts.get(FontRole::Mono, 14.f));
}

namespace {
// Column x of a bitmap, top to bottom (the alpha of each pixel).
std::vector<std::uint8_t> column(const prism::render::TextBitmap& b, int x) {
    std::vector<std::uint8_t> out;
    for (int y = 0; y < b.height; ++y) out.push_back(b.rgba[(static_cast<size_t>(y) * b.width + x) * 4 + 3]);
    return out;
}
const std::string hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";   // "shalom"
}

TEST_CASE("in a Hebrew line, an English word is drawn on the left (bidi visual order)") {
    // The test fonts have no Hebrew: its letters are boxes. Where "ab" lands still shows the order.
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    auto ab = fonts.rasterize("ab", 14.f, FontRole::Ui);
    auto line = fonts.rasterize(hebrew + " ab", 14.f, FontRole::Ui);
    REQUIRE(ab);
    REQUIRE(line);
    REQUIRE(line->width > ab->width);
    REQUIRE(line->height == ab->height);
    for (int x = 0; x < ab->width; ++x) CHECK(column(*line, x) == column(*ab, x));
}

TEST_CASE("mixed-direction text is as wide as each run measured on its own") {
    // The caret math adds the runs' widths; the drawing must take the same room, spaces included.
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    TTF_Font* f = fonts.get(FontRole::Ui, 14.f);
    std::string line = "ab " + hebrew + " cd";
    int w_line = 0, w_parts = 0;
    for (std::string_view part : {std::string_view{"ab "}, std::string_view{hebrew}, std::string_view{" cd"}}) {
        int w = 0, h = 0;
        TTF_GetStringSize(f, part.data(), part.size(), &w, &h);
        w_parts += w;
    }
    auto bmp = fonts.rasterize(line, 14.f, FontRole::Ui);
    REQUIRE(bmp);
    w_line = bmp->width;
    CHECK(w_line == w_parts);
}
