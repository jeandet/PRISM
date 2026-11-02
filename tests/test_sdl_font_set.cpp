#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_font_set.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cstdlib>

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

TEST_CASE("Hebrew and Arabic letters draw from the bundled script fonts, not as boxes") {
    // Found next to the UI font. Without them, two different letters draw the same empty box.
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    auto differ = [&](std::string a, std::string b) {
        auto ba = fonts.rasterize(a, 14.f, FontRole::Ui), bb = fonts.rasterize(b, 14.f, FontRole::Ui);
        REQUIRE(ba);
        REQUIRE(bb);
        return ba->width != bb->width || ba->rgba != bb->rgba;
    };
    CHECK(differ("\xd7\x90", "\xd7\x91"));   // alef, bet
    CHECK(differ("\xd8\xa8", "\xd8\xb3"));   // beh, seen
}

TEST_CASE("an English word after a Hebrew one keeps its letter order (a right-to-left line)") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    auto word = fonts.rasterize("world", 14.f, FontRole::Ui);
    auto line = fonts.rasterize(hebrew + " world", 14.f, FontRole::Ui);
    REQUIRE(word);
    REQUIRE(line);
    REQUIRE(line->height == word->height);
    for (int x = 0; x < word->width; ++x) CHECK(column(*line, x) == column(*word, x));
}

namespace {
// The lowest row with ink in columns [x0, x1) of a bitmap, or -1.
int ink_bottom(const prism::render::TextBitmap& b, int x0, int x1) {
    int bottom = -1;
    for (int y = 0; y < b.height; ++y)
        for (int x = x0; x < x1; ++x)
            if (b.rgba[(static_cast<size_t>(y) * b.width + x) * 4 + 3] > 100) bottom = y;
    return bottom;
}
}

TEST_CASE("a run from another script's font sits on the line's baseline") {
    // SDL_ttf places a fallback font's glyphs on that font's ascent; Noto Sans Arabic's is
    // larger, so drawn as is an Arabic word sat lower than the Latin text beside it.
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    auto x = fonts.rasterize("x ", 14.f, FontRole::Ui);
    auto line = fonts.rasterize("x \xd8\xa7", 14.f, FontRole::Ui);   // x, a space, alef (a stroke on the baseline)
    REQUIRE(x);
    REQUIRE(line);
    int latin = ink_bottom(*line, 0, x->width);
    int arabic = ink_bottom(*line, x->width, line->width);
    REQUIRE(latin >= 0);
    REQUIRE(arabic >= 0);
    CHECK(std::abs(latin - arabic) <= 1);
    CHECK(latin == ink_bottom(*x, 0, x->width));   // the Latin text did not move
}

TEST_CASE("a Hebrew word reads right to left: its first letter is its rightmost") {
    TtfSession ttf;
    prism::backends::SdlFontSet fonts;
    REQUIRE(fonts.open(PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH, 14.f));
    auto shin = fonts.rasterize("\xd7\xa9", 14.f, FontRole::Ui);   // the word's first letter alone
    auto word = fonts.rasterize(hebrew, 14.f, FontRole::Ui);
    REQUIRE(shin);
    REQUIRE(word);
    int differing = 0;   // shin's columns are the word's last ones
    for (int x = 0; x < shin->width; ++x)
        differing += column(*word, word->width - shin->width + x) != column(*shin, x);
    CHECK(differing <= 1);   // a glyph edge may touch its neighbour's column
}
