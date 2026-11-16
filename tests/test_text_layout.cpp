// text::layout_block: a block's text and spans into lines of styled runs (doc/design/text-stack.md).
// A fake measurer makes widths exact: 10 per byte, 11 when bold; line height 1.5 x size.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/layout.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::text;

namespace {
struct Fixed : ui::TextMeasurer {
    float width(std::string_view s, float size, render::FontStyle f) const override {
        return static_cast<float>(s.size()) * (f.bold ? 11.f : 10.f) * size / 14.f;
    }
    float line_height(float size, render::FontStyle) const override { return 1.5f * size; }
};
const Fixed fixed;
const ui::Theme theme{};

LayoutStyle prose(float width) { return {.measurer = &fixed, .theme = &theme, .size = 14.f, .width = width}; }

std::string line_text(const Block& b, const Line& l) { return b.text.substr(l.begin, l.end - l.begin); }
} // namespace

TEST_CASE("a short paragraph is one line and one run") {
    Block b{.text = "hello"};
    auto out = layout_block(b, {}, prose(500));
    REQUIRE(out.lines.size() == 1);
    REQUIRE(out.lines[0].runs.size() == 1);
    CHECK(out.lines[0].runs[0].width == doctest::Approx(50));
    CHECK(out.lines[0].height == doctest::Approx(21));
    CHECK(out.lines[0].runs[0].look.color == theme.text);
}

TEST_CASE("a paragraph wraps between words; the space hangs at the end of the line") {
    Block b{.text = "aaaa bbbb cccc"};
    auto out = layout_block(b, {}, prose(100));
    REQUIRE(out.lines.size() == 2);
    CHECK(line_text(b, out.lines[0]) == "aaaa bbbb ");
    CHECK(line_text(b, out.lines[1]) == "cccc");
    CHECK(out.lines[1].y == doctest::Approx(out.lines[0].height));
}

TEST_CASE("a word longer than the line breaks inside it") {
    Block b{.text = "abcdefghijklmnop"};
    auto out = layout_block(b, {}, prose(60));
    REQUIRE(out.lines.size() == 3);
    CHECK(line_text(b, out.lines[0]) == "abcdef");
}

TEST_CASE("without wrapping a long line stays one line") {
    Block b{.text = "aaaa bbbb cccc dddd"};
    auto style = prose(40);
    style.wrap = false;
    CHECK(layout_block(b, {}, style).lines.size() == 1);
}

TEST_CASE("a bold span is its own run, in a bold face, placed after the run before it") {
    Block b{.text = "say hello now", .spans = {{4, 9, Style::strong}}};
    auto out = layout_block(b, {}, prose(500));
    const auto& runs = out.lines[0].runs;
    REQUIRE(runs.size() == 3);
    CHECK(runs[1].look.font.bold);
    CHECK(runs[1].x == doctest::Approx(40));
    CHECK(runs[1].width == doctest::Approx(55));
    CHECK(runs[2].x == doctest::Approx(95));
    CHECK_FALSE(runs[2].look.font.bold);
}

TEST_CASE("a heading is bold and larger; a code block is mono on a background") {
    Block h{.text = "Title", .kind = BlockKind::heading, .level = 1};
    auto out = layout_block(h, {}, prose(500));
    CHECK(out.lines[0].runs[0].look.font.bold);
    CHECK(out.lines[0].runs[0].look.size > 14.f);
    CHECK(out.lines[0].height > 21.f);

    Block code{.text = "int x;", .kind = BlockKind::code};
    auto c = layout_block(code, {}, prose(500));
    CHECK(c.lines[0].runs[0].look.font.family == render::FontRole::Mono);
    CHECK(c.background.has_value());
}

TEST_CASE("computed spans (a highlighter's) color the text without being stored") {
    Block b{.text = "int x;"};
    const std::vector<Span> keyword{{0, 3, Style::keyword}};
    auto out = layout_block(b, keyword, prose(500));
    REQUIRE(out.lines[0].runs.size() == 2);
    CHECK(out.lines[0].runs[0].look.color == theme.syntax_keyword);
    CHECK(b.spans.empty());
}

TEST_CASE("an empty block is one empty line, as tall as text, so a caret can sit in it") {
    auto out = layout_block(Block{}, {}, prose(500));
    REQUIRE(out.lines.size() == 1);
    CHECK(out.lines[0].height == doctest::Approx(21));
    CHECK(out.height >= 21.f);
}

TEST_CASE("x of a byte, and the byte at an x, within a laid out line") {
    Block b{.text = "say hello", .spans = {{4, 9, Style::strong}}};
    auto out = layout_block(b, {}, prose(500));
    const auto& line = out.lines[0];
    CHECK(x_of(b, line, 2, prose(500)) == doctest::Approx(20));
    CHECK(x_of(b, line, 6, prose(500)) == doctest::Approx(62));   // 40 + 2 bold bytes
    CHECK(byte_at(b, line, 63, prose(500)) == 6);
    CHECK(byte_at(b, line, 1000, prose(500)) == 9);
}
