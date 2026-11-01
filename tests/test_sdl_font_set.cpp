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
