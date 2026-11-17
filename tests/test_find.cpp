// text::find_all, find_next and replace_all over a Document.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/find.hpp>

#include <string>

using namespace prism::text;

TEST_CASE("find_all: every match, block by block, in order") {
    Document doc{"one two one\nnone\ntwo"};
    auto hits = find_all(doc, "one");
    REQUIRE(hits.size() == 3);
    CHECK(hits[0] == Range{{0, 0}, {0, 3}});
    CHECK(hits[1] == Range{{0, 8}, {0, 11}});
    CHECK(hits[2] == Range{{1, 1}, {1, 4}});
    CHECK(find_all(doc, "").empty());
    CHECK(find_all(doc, "zzz").empty());
}

TEST_CASE("ignoring case, and whole words only") {
    Document doc{"Select select SELECTION"};
    CHECK(find_all(doc, "select").size() == 1);
    CHECK(find_all(doc, "select", {.case_sensitive = false}).size() == 3);
    CHECK(find_all(doc, "select", {.case_sensitive = false, .whole_word = true}).size() == 2);
}

TEST_CASE("find_next goes on from a position, and wraps around at the end") {
    Document doc{"a x b\nx c"};
    CHECK(find_next(doc, "x", {0, 0}) == Range{{0, 2}, {0, 3}});
    CHECK(find_next(doc, "x", {0, 3}) == Range{{1, 0}, {1, 1}});
    CHECK(find_next(doc, "x", {1, 1}) == Range{{0, 2}, {0, 3}});   // wrapped
    CHECK_FALSE(find_next(doc, "y", {0, 0}));
}

TEST_CASE("replace_all replaces every match as one undo step, keeping spans in place") {
    Document doc{"cat and cat\ndog and cat"};
    doc.style({{1, 0}, {1, 3}}, Style::strong);   // "dog" in bold
    CHECK(replace_all(doc, "cat", "lion") == 3);
    CHECK(doc.text() == "lion and lion\ndog and lion");
    REQUIRE(doc[1].spans.size() == 1);
    CHECK(doc[1].spans[0].end == 3);
    CHECK(doc.undo());
    CHECK(doc.text() == "cat and cat\ndog and cat");
    CHECK(replace_all(doc, "zzz", "y") == 0);
}
