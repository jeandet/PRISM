// text::markdown: GitHub-flavoured Markdown into a text::Document, parsed by cmark-gfm.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/markdown.hpp>

#include <string>

using namespace prism::text;

namespace {
const Span* span_of(const Block& b, Style s) {
    for (const auto& sp : b.spans)
        if (sp.style == s) return &sp;
    return nullptr;
}
std::string spanned(const Block& b, Style s) {
    const Span* sp = span_of(b, s);
    return sp ? b.text.substr(sp->begin, sp->end - sp->begin) : std::string{};
}
} // namespace

TEST_CASE("headings keep their level; paragraphs their text") {
    auto doc = markdown("# Title\n\nSome text.\n\n### Small");
    REQUIRE(doc.size() == 3);
    CHECK(doc[0].kind == BlockKind::heading);
    CHECK(doc[0].level == 1);
    CHECK(doc[0].text == "Title");
    CHECK(doc[1].kind == BlockKind::paragraph);
    CHECK(doc[1].text == "Some text.");
    CHECK(doc[2].level == 3);
}

TEST_CASE("inline styles become spans over the right words") {
    auto doc = markdown("An *em*, a **strong**, some `code`, a [link](https://x.org) and ~~gone~~.");
    const Block& b = doc[0];
    CHECK(b.text == "An em, a strong, some code, a link and gone.");
    CHECK(spanned(b, Style::emphasis) == "em");
    CHECK(spanned(b, Style::strong) == "strong");
    CHECK(spanned(b, Style::code) == "code");
    CHECK(spanned(b, Style::link) == "link");
    CHECK(span_of(b, Style::link)->target == "https://x.org");
    CHECK(spanned(b, Style::strike) == "gone");
}

TEST_CASE("a soft line break inside a paragraph is a space") {
    auto doc = markdown("one\ntwo");
    REQUIRE(doc.size() == 1);
    CHECK(doc[0].text == "one two");
}

TEST_CASE("lists: bullets, numbers, nesting, and task boxes") {
    auto doc = markdown("- a\n- b\n  - inner\n\n1. one\n2. two\n\n- [x] done\n- [ ] todo");
    REQUIRE(doc.size() == 7);
    CHECK(doc[0].kind == BlockKind::bullet);
    CHECK(doc[0].text == "a");
    CHECK(doc[0].info == "\xE2\x80\xA2");
    CHECK(doc[2].text == "inner");
    CHECK(doc[2].level == 1);
    CHECK(doc[3].kind == BlockKind::ordered);
    CHECK(doc[3].info == "1.");
    CHECK(doc[4].info == "2.");
    CHECK(doc[5].text == "done");
    CHECK(doc[5].info == "\xE2\x96\xA0");   // checked box: a filled square
    CHECK(doc[6].info == "\xE2\x96\xA1");   // empty box
}

TEST_CASE("a fenced code block is one block per line, with its language") {
    auto doc = markdown("```cpp\nint x;\nint y;\n```");
    REQUIRE(doc.size() == 2);
    CHECK(doc[0].kind == BlockKind::code);
    CHECK(doc[0].text == "int x;");
    CHECK(doc[1].text == "int y;");
    CHECK(doc[1].info == "cpp");
}

TEST_CASE("quotes and rules") {
    auto doc = markdown("> quoted\n\n---\n\nafter");
    REQUIRE(doc.size() == 3);
    CHECK(doc[0].kind == BlockKind::quote);
    CHECK(doc[0].text == "quoted");
    CHECK(doc[1].kind == BlockKind::rule);
    CHECK(doc[2].text == "after");
}

TEST_CASE("a table is a row per block, cells apart by tabs, the header row marked") {
    auto doc = markdown("| a | **b** |\n|---|---|\n| 1 | 2 |");
    REQUIRE(doc.size() == 2);
    CHECK(doc[0].kind == BlockKind::table_row);
    CHECK(doc[0].level == 1);              // the header
    CHECK(doc[0].text == "a\tb");
    CHECK(spanned(doc[0], Style::strong) == "b");
    CHECK(doc[1].kind == BlockKind::table_row);
    CHECK(doc[1].level == 0);
    CHECK(doc[1].text == "1\t2");
}

TEST_CASE("empty input is one empty block") {
    auto doc = markdown("");
    CHECK(doc.size() == 1);
    CHECK(doc[0].text.empty());
}

TEST_CASE("review: an empty list item is kept, with its marker") {
    auto doc = markdown("- a\n-\n- c");
    REQUIRE(doc.size() == 3);
    CHECK(doc[1].kind == BlockKind::bullet);
    CHECK(doc[1].text.empty());
    CHECK(doc[1].info == "\xE2\x80\xA2");
}
