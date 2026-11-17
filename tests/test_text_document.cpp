// text::Document (doc/design/text-stack.md): blocks of text with styled spans, edited in place,
// observed, and undone. Checked against a plain std::string doing the same edits.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/document.hpp>

#include <random>
#include <string>
#include <vector>

using namespace prism::text;

TEST_CASE("a text becomes one block per line, and comes back the same") {
    Document doc{"first\nsecond\n\nfourth"};
    REQUIRE(doc.size() == 4);
    CHECK(doc[1].text == "second");
    CHECK(doc[2].text.empty());
    CHECK(doc.text() == "first\nsecond\n\nfourth");
    CHECK(doc.bytes() == 20);
    CHECK(Document{}.size() == 1);   // an empty document still has a block to type in
}

TEST_CASE("positions and byte offsets convert both ways") {
    Document doc{"ab\ncde\nf"};
    CHECK(doc.position_at(0) == Position{0, 0});
    CHECK(doc.position_at(2) == Position{0, 2});
    CHECK(doc.position_at(3) == Position{1, 0});
    CHECK(doc.position_at(8) == Position{2, 1});
    CHECK(doc.offset_of({1, 2}) == 5);
    CHECK(doc.end() == Position{2, 1});
}

TEST_CASE("inserting within a line, and across lines") {
    Document doc{"hello world"};
    CHECK(doc.insert({0, 5}, ",") == Position{0, 6});
    CHECK(doc.text() == "hello, world");
    CHECK(doc.insert({0, 6}, "\nbig\n") == Position{2, 0});
    CHECK(doc.text() == "hello,\nbig\n world");
    CHECK(doc.size() == 3);
}

TEST_CASE("erasing within a line, and across lines joins them") {
    Document doc{"one\ntwo\nthree"};
    doc.erase({{0, 1}, {0, 3}});
    CHECK(doc.text() == "o\ntwo\nthree");
    doc.erase({{0, 1}, {2, 2}});
    CHECK(doc.text() == "oree");
    CHECK(doc.size() == 1);
}

TEST_CASE("each edit says which blocks changed") {
    Document doc{"a\nb\nc"};
    std::vector<Change> seen;
    auto c = doc.on_change().connect([&](const Change& ch) { seen.push_back(ch); });
    doc.insert({1, 1}, "x");
    doc.insert({1, 0}, "\n");
    doc.erase({{0, 1}, {2, 0}});
    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == Change{1, 1, 1});   // block 1 replaced by one block
    CHECK(seen[1] == Change{1, 1, 2});   // block 1 became two
    CHECK(seen[2] == Change{0, 3, 1});   // blocks 0..2 joined into one
}

TEST_CASE("an edited block gets a new version; the others keep theirs") {
    Document doc{"a\nb"};
    const auto a = doc[0].version, b = doc[1].version;
    doc.insert({1, 0}, "x");
    CHECK(doc[0].version == a);
    CHECK(doc[1].version != b);
}

TEST_CASE("spans move with the text: bold grows when typed at its end, a link does not") {
    Document doc{"say hello"};
    doc.style({{0, 4}, {0, 9}}, Style::strong);
    doc.style({{0, 0}, {0, 3}}, Style::link);
    doc.insert({0, 9}, "!");          // end of bold: bold grows
    doc.insert({0, 3}, "s");          // end of the link: the link stays "say"
    doc.insert({0, 0}, ">");          // before everything: both move
    // ">says hello!": link [1,4), bold [6,12)
    const auto& spans = doc[0].spans;
    REQUIRE(spans.size() == 2);
    auto find = [&](Style s) {
        for (const auto& sp : spans) if (sp.style == s) return sp;
        return Span{};
    };
    CHECK(find(Style::link).begin == 1);
    CHECK(find(Style::link).end == 4);
    CHECK(find(Style::strong).begin == 6);
    CHECK(find(Style::strong).end == 12);
}

TEST_CASE("erasing a styled range removes its span; erasing part of it shrinks it") {
    Document doc{"plain bold plain"};
    doc.style({{0, 6}, {0, 10}}, Style::strong);
    doc.erase({{0, 7}, {0, 9}});
    REQUIRE(doc[0].spans.size() == 1);
    CHECK(doc[0].spans[0].begin == 6);
    CHECK(doc[0].spans[0].end == 8);
    doc.erase({{0, 5}, {0, 9}});
    CHECK(doc[0].spans.empty());
}

TEST_CASE("unstyle removes a style from a range, splitting a span around it") {
    Document doc{"abcdef"};
    doc.style({{0, 0}, {0, 6}}, Style::emphasis);
    doc.unstyle({{0, 2}, {0, 4}}, Style::emphasis);
    REQUIRE(doc[0].spans.size() == 2);
    CHECK(doc[0].spans[0].end == 2);
    CHECK(doc[0].spans[1].begin == 4);
}

TEST_CASE("undo and redo: typing is one step until a break") {
    Document doc{"x"};
    doc.insert({0, 1}, "a");
    doc.insert({0, 2}, "b");
    doc.insert({0, 3}, "c");
    doc.break_undo_group();
    doc.insert({0, 4}, "\nnew line");
    CHECK(doc.text() == "xabc\nnew line");
    CHECK(doc.undo());
    CHECK(doc.text() == "xabc");
    CHECK(doc.undo());
    CHECK(doc.text() == "x");
    CHECK_FALSE(doc.undo());
    CHECK(doc.redo());
    CHECK(doc.text() == "xabc");
    doc.insert({0, 0}, "!");          // a new edit drops what could be redone
    CHECK_FALSE(doc.redo());
}

TEST_CASE("undo restores styles and block kinds too") {
    Document doc{"title"};
    doc.set_kind(0, BlockKind::heading, 1);
    doc.break_undo_group();
    doc.style({{0, 0}, {0, 5}}, Style::emphasis);
    CHECK(doc.undo());
    CHECK(doc[0].spans.empty());
    CHECK(doc[0].kind == BlockKind::heading);
    CHECK(doc.undo());
    CHECK(doc[0].kind == BlockKind::paragraph);
}

TEST_CASE("thousands of random edits agree with a string, and undo all the way back") {
    std::mt19937 rng(42);
    const std::string original = "line one\nline two\n\nline four\nend";
    Document doc{original};
    std::string ref = original;
    const char* pieces[] = {"x", "hello", "\n", "a\nb", "\n\n", "", "tab\there"};
    for (int step = 0; step < 4000; ++step) {
        if (step % 7 == 0) doc.break_undo_group();
        if (rng() % 3 != 0 || ref.empty()) {
            const std::size_t at = rng() % (ref.size() + 1);
            const std::string piece = pieces[rng() % std::size(pieces)];
            doc.insert(doc.position_at(at), piece);
            ref.insert(at, piece);
        } else {
            const std::size_t at = rng() % ref.size();
            const std::size_t n = std::min<std::size_t>(1 + rng() % 12, ref.size() - at);
            doc.erase({doc.position_at(at), doc.position_at(at + n)});
            ref.erase(at, n);
        }
        if (step % 500 == 0) REQUIRE(doc.text() == ref);
    }
    REQUIRE(doc.text() == ref);
    CHECK(doc.bytes() == ref.size());
    while (doc.undo()) {}
    CHECK(doc.text() == original);
}

TEST_CASE("assign replaces everything as one edit: watchers hear it, and undo brings the old back") {
    Document doc{"old\ntext"};
    std::vector<Change> seen;
    auto c = doc.on_change().connect([&](const Change& ch) { seen.push_back(ch); });
    doc.assign(Document{std::vector<Block>{{.text = "Title", .kind = BlockKind::heading, .level = 1}, {.text = "body"}}});
    CHECK(doc.text() == "Title\nbody");
    CHECK(doc[0].kind == BlockKind::heading);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0] == Change{0, 2, 2});
    CHECK(doc.undo());
    CHECK(doc.text() == "old\ntext");
}

#include <prism/core/derived.hpp>

TEST_CASE("reset loads new text with no undo back to the old one") {
    Document doc{"old"};
    doc.insert({0, 3}, "!");
    doc.reset(Document{"a new file"});
    CHECK(doc.text() == "a new file");
    CHECK_FALSE(doc.can_undo());
}

TEST_CASE("a Derived reading a document follows it, with no list") {
    Document doc{"one two"};
    prism::core::Derived<std::size_t> length{[&] { return doc.text().size(); }};
    CHECK(length.get() == 7);
    doc.insert({0, 7}, " three");
    CHECK(length.get() == 13);
}

TEST_CASE("styling, unstyling and changing a kind keep the number of blocks") {
    Document doc{"one\ntwo\nthree"};
    doc.style({{1, 0}, {2, 2}}, Style::strong);
    CHECK(doc.size() == 3);
    doc.unstyle({{1, 1}, {1, 2}}, Style::strong);
    CHECK(doc.size() == 3);
    doc.set_kind(0, BlockKind::heading, 1);
    CHECK(doc.size() == 3);
    CHECK(doc.text() == "one\ntwo\nthree");
}
