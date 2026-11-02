// Unicode text boundaries (ui/text_segmentation.hpp): graphemes (UAX #29) and line breaks (UAX #14).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/text_segmentation.hpp>

using namespace prism::ui;

namespace {
const std::string e_acute = "e\xcc\x81";                       // e + combining acute: 3 bytes, 1 character
const std::string family = "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7";   // man ZWJ woman ZWJ girl
}

TEST_CASE("a base letter and its combining accent are one step") {
    std::string s = e_acute + "x";
    CHECK(next_grapheme(s, 0) == 3);
    CHECK(prev_grapheme(s, 3) == 0);
    CHECK(next_grapheme(s, 3) == 4);
}

TEST_CASE("an emoji family joined by zero-width joiners is one step") {
    std::string s = "a" + family + "b";
    CHECK(next_grapheme(s, 1) == 1 + family.size());
    CHECK(prev_grapheme(s, 1 + family.size()) == 1);
}

TEST_CASE("grapheme steps stop at the ends") {
    CHECK(next_grapheme("ab", 2) == 2);
    CHECK(prev_grapheme("ab", 0) == 0);
    CHECK(next_grapheme("", 0) == 0);
}

TEST_CASE("line breaks: after spaces and hyphens, between ideographs, never inside a word") {
    auto ok = line_break_opportunities("well-known words");
    // a break may come after "well-" (5) and after "known " (11); not inside "known"
    CHECK(ok[5]);
    CHECK(ok[11]);
    CHECK_FALSE(ok[3]);
    CHECK_FALSE(ok[8]);
    std::string cjk = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";   // 日本語: three ideographs, 3 bytes each
    auto c = line_break_opportunities(cjk);
    CHECK(c[3]);
    CHECK(c[6]);
    CHECK_FALSE(c[1]);   // never inside a character
}

// --- the caret and deletion step by character, not by code point ---

#include <prism/delegates/text_delegates.hpp>

TEST_CASE("Right and Left move over a whole character; Backspace and Delete remove one") {
    using prism::ui::TextEdit;
    using namespace prism::input;
    std::string s = "a" + e_acute + "b";   // a, é (2 code points), b
    TextEdit at1{s, 1, 1};
    auto right = prism::ui::text_detail::common_edit_key(at1, KeyPress{keys::right, 0});
    REQUIRE(right);
    CHECK(right->cursor == 4);
    auto left = prism::ui::text_detail::common_edit_key(TextEdit{s, 4, 4}, KeyPress{keys::left, 0});
    REQUIRE(left);
    CHECK(left->cursor == 1);
    CHECK(prism::ui::text_edit::erase_back(TextEdit{s, 4, 4}).text == "ab");
    CHECK(prism::ui::text_edit::erase_forward(TextEdit{s, 1, 1}).text == "ab");
    std::string f = family + "x";
    CHECK(prism::ui::text_edit::erase_back(TextEdit{f, family.size(), family.size()}).text == "x");
}

// --- word wrap breaks where UAX #14 allows ---

#include <prism/ui/text_metrics.hpp>

namespace {
struct PerByte final : prism::ui::TextMeasurer {   // 10 px per byte
    float width(std::string_view t, float, prism::render::FontRole) const override { return 10.f * static_cast<float>(t.size()); }
};
std::vector<std::string> wrapped(const std::string& text, float width) {
    static PerByte m;
    std::vector<std::string> out;
    for (auto span : prism::ui::wrap_lines(text, prism::core::Width{width}, &m, 14.f, prism::render::FontRole::Ui))
        out.push_back(text.substr(span.start, span.length));
    return out;
}
}

TEST_CASE("wrap breaks after a hyphen when the word does not fit") {
    CHECK(wrapped("well-known", 70.f) == std::vector<std::string>{"well-", "known"});
}

TEST_CASE("wrap breaks between ideographs, which have no spaces") {
    std::string cjk = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x81\xa7\xe3\x81\x99";   // 日本語です: 5 x 3 bytes
    auto lines = wrapped(cjk, 70.f);   // two ideographs (60 px) per line
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "\xe6\x97\xa5\xe6\x9c\xac");
}

TEST_CASE("wrap still hangs the spaces after a word on its line") {
    CHECK(wrapped("aaa   bbb", 40.f) == std::vector<std::string>{"aaa   ", "bbb"});
}
