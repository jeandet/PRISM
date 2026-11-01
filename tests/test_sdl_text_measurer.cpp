#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/backends/sdl_text_measurer.hpp>

#include <SDL3_ttf/SDL_ttf.h>

#include <cmath>
#include <optional>

using prism::render::FontRole;

namespace {
struct TtfSession {   // TTF needs no video subsystem, so this runs without a display
    TtfSession() { TTF_Init(); }
    ~TtfSession() { TTF_Quit(); }
};
prism::backends::SdlTextMeasurer bundled() {
    return prism::backends::SdlTextMeasurer{PRISM_TEST_UI_FONT_PATH, PRISM_TEST_MONO_FONT_PATH};
}
const char* bullet = "\xE2\x97\x8F";   // U+25CF: not in Noto Sans, comes from the mono fallback
}

TEST_CASE("mono role: JetBrains Mono, 0.6 em per character") {
    TtfSession ttf;
    auto m = bundled();
    CHECK(m.width("", 14.f, FontRole::Mono) == 0.f);
    CHECK(m.width("abc", 14.f, FontRole::Mono) == doctest::Approx(25.2f).epsilon(0.05));
    CHECK(m.width("\xC3\xA9", 14.f, FontRole::Mono) == m.width("a", 14.f, FontRole::Mono));
    CHECK(m.width("iiii", 14.f, FontRole::Mono) == m.width("MMMM", 14.f, FontRole::Mono));
}

TEST_CASE("ui role: proportional Noto Sans") {
    TtfSession ttf;
    auto m = bundled();
    CHECK(m.width("MMMM", 14.f, FontRole::Ui) > 1.5f * m.width("iiii", 14.f, FontRole::Ui));
    CHECK(m.width("abc", 28.f, FontRole::Ui) > m.width("abc", 14.f, FontRole::Ui));
}

TEST_CASE("ui role falls back to the mono font for symbols Noto Sans lacks") {
    TtfSession ttf;
    auto m = bundled();
    CHECK(m.width(bullet, 14.f, FontRole::Ui) == m.width(bullet, 14.f, FontRole::Mono));
}

TEST_CASE("the fallback font follows size changes") {
    TtfSession ttf;
    auto m = bundled();
    float small = m.width(bullet, 14.f, FontRole::Ui);
    float big = m.width(bullet, 28.f, FontRole::Ui);
    CHECK(small > 0.f);
    CHECK(big == doctest::Approx(2.f * small).epsilon(0.15));
}

TEST_CASE("without usable fonts, measurement falls back to the estimate") {
    TtfSession ttf;
    prism::backends::SdlTextMeasurer m{"/nonexistent/ui.ttf", "/nonexistent/mono.ttf"};
    CHECK(m.width("abc", 10.f, FontRole::Ui) == doctest::Approx(18.f));
}

TEST_CASE("measuring before TTF_Init does not disable real measurement later") {
    std::optional<TtfSession> ttf;               // declared first: TTF must outlive the measurer
    auto m = bundled();
    CHECK(m.width("abc", 14.f, FontRole::Mono) == 25.2f);   // estimate: TTF not initialized yet
    ttf.emplace();
    // Same string again: the pre-init estimate must not have been cached.
    CHECK(m.width("abc", 14.f, FontRole::Mono) == std::round(m.width("abc", 14.f, FontRole::Mono)));
    CHECK(m.width("abc", 14.f, FontRole::Mono) != 25.2f);
}

TEST_CASE("line height comes from the font's own line spacing") {
    TtfSession ttf;
    auto m = bundled();
    float mono = m.line_height(14.f, FontRole::Mono);
    CHECK(mono == std::round(mono));                  // SDL_ttf reports whole pixels
    CHECK(mono > 14.f);
    CHECK(mono != doctest::Approx(14.f * 1.4f));      // not the estimate
    CHECK(m.line_height(28.f, FontRole::Ui) > m.line_height(14.f, FontRole::Ui));
}

TEST_CASE("measured widths scale linearly, so HiDPI rendering matches the measurement") {
    // The renderer rasterizes at size x device scale and scales down; the measurer measures at
    // the point size. Whole-pixel hinted advances made those disagree by ~6% at 2x.
    TtfSession ttf;
    auto m = bundled();
    const char* line = "Each subfolder is a standalone executable built alongside the library, plus";
    for (FontRole role : {FontRole::Mono, FontRole::Ui}) {
        float at_1x = m.width(line, 14.f, role);
        for (float scale : {1.25f, 1.5f, 2.f}) {   // browsers often run at fractional scales
            CAPTURE(scale);
            CHECK(m.width(line, 14.f * scale, role) / scale == doctest::Approx(at_1x).epsilon(0.005));
        }
    }
}
