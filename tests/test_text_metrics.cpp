#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/text_metrics.hpp>

#include <string_view>
#include <vector>

namespace {
// Proportional stub: 'i' = 2, 'm' = 10, any other code point = 6 (size ignored).
struct StubMeasurer final : prism::ui::TextMeasurer {
    float width(std::string_view s, float, prism::render::FontRole) const override {
        float w = 0;
        for (size_t i = 0; i < s.size(); i = prism::core::utf8::next(s, i))
            w += s[i] == 'i' ? 2.f : s[i] == 'm' ? 10.f : 6.f;
        return w;
    }
};
}

TEST_CASE("without a measurer, text width falls back to 0.6 x size per character") {
    CHECK(prism::ui::text_width(nullptr, "abc", 10.f) == doctest::Approx(18.f));
    CHECK(prism::ui::text_width(nullptr, "\xC3\xA9\xC3\xA9", 10.f) == doctest::Approx(12.f));
}

TEST_CASE("prefix_width measures up to a byte offset, snapped to a character start") {
    StubMeasurer m;
    CHECK(prism::ui::prefix_width(&m, "mim", 2, 14.f) == doctest::Approx(12.f));
    CHECK(prism::ui::prefix_width(&m, "\xC3\xA9m", 1, 14.f) == doctest::Approx(0.f));   // mid-"é" snaps to 0
}

TEST_CASE("index_at returns the nearest character boundary to an x position") {
    StubMeasurer m;   // "mim": boundaries at x = 0, 10, 12, 22
    CHECK(prism::ui::index_at(&m, "mim", 14.f, -5.f) == 0);
    CHECK(prism::ui::index_at(&m, "mim", 14.f, 4.f) == 0);
    CHECK(prism::ui::index_at(&m, "mim", 14.f, 6.f) == 1);
    CHECK(prism::ui::index_at(&m, "mim", 14.f, 11.5f) == 2);
    CHECK(prism::ui::index_at(&m, "mim", 14.f, 99.f) == 3);
    CHECK(prism::ui::index_at(&m, "", 14.f, 5.f) == 0);
    CHECK(prism::ui::index_at(&m, "mim", 14.f, 11.f) == 2);   // exact midpoint of 10 and 12: later wins
}

TEST_CASE("fit_bytes returns the longest prefix that fits, at least one character") {
    StubMeasurer m;
    CHECK(prism::ui::fit_bytes(&m, "mmii", 14.f, 21.f) == 2);    // "mm" = 20 fits, "mmi" = 22 does not
    CHECK(prism::ui::fit_bytes(&m, "mmii", 14.f, 5.f) == 1);     // never zero: always make progress
    CHECK(prism::ui::fit_bytes(&m, "mmii", 14.f, 999.f) == 4);
}

TEST_CASE("measurement helpers pass the font role through, UI by default") {
    struct Recorder final : prism::ui::TextMeasurer {
        mutable std::vector<prism::render::FontRole> roles;
        float width(std::string_view s, float, prism::render::FontRole r) const override {
            roles.push_back(r);
            return static_cast<float>(s.size());
        }
    } m;
    prism::ui::text_width(&m, "a", 14.f);
    prism::ui::prefix_width(&m, "ab", 1, 14.f, prism::render::FontRole::Mono);
    prism::ui::index_at(&m, "ab", 14.f, 1.f, prism::render::FontRole::Mono);
    prism::ui::fit_bytes(&m, "ab", 14.f, 5.f, prism::render::FontRole::Mono);
    REQUIRE(m.roles.size() >= 4);
    CHECK(m.roles.front() == prism::render::FontRole::Ui);
    for (size_t i = 1; i < m.roles.size(); ++i) CHECK(m.roles[i] == prism::render::FontRole::Mono);
}
