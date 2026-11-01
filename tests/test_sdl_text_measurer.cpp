#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_text_measurer.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <optional>

namespace {
struct TtfSession {   // TTF needs no video subsystem, so this runs without a display
    TtfSession() { TTF_Init(); }
    ~TtfSession() { TTF_Quit(); }
};
}

TEST_CASE("SdlTextMeasurer measures the bundled font for real") {
    TtfSession ttf;
    {
        prism::backends::SdlTextMeasurer m{PRISM_TEST_FONT_PATH};
        CHECK(m.width("", 14.f) == 0.f);
        // JetBrains Mono advances 0.6 em: 3 x 8.4 = 25.2 px, rounded by SDL_ttf.
        CHECK(m.width("abc", 14.f) == doctest::Approx(25.2f).epsilon(0.05));
        CHECK(m.width("\xC3\xA9", 14.f) == m.width("a", 14.f));   // one character, not two bytes
        CHECK(m.width("abc", 28.f) > m.width("abc", 14.f));       // size is honored
        CHECK(m.width("abc", 14.f) == m.width("abc", 14.f));      // stable (cached)
    }
}

TEST_CASE("SdlTextMeasurer without a usable font falls back to the estimate") {
    TtfSession ttf;
    prism::backends::SdlTextMeasurer m{"/nonexistent/font.ttf"};
    CHECK(m.width("abc", 10.f) == doctest::Approx(18.f));
}

TEST_CASE("measuring before TTF_Init does not disable real measurement later") {
    std::optional<TtfSession> ttf;               // declared first: TTF must outlive the measurer
    prism::backends::SdlTextMeasurer m{PRISM_TEST_FONT_PATH};
    CHECK(m.width("abc", 14.f) == 25.2f);        // estimate: TTF not initialized yet
    ttf.emplace();
    // Same string again: the pre-init estimate must not have been cached.
    CHECK(m.width("abc", 14.f) == std::round(m.width("abc", 14.f)));   // real: whole pixels
    CHECK(m.width("abc", 14.f) != 25.2f);
}
