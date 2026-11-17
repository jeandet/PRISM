// Rich text editing: style commands, Markdown shortcuts as you type, lists that continue, and a
// document written back as Markdown.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/editor.hpp>
#include <prism/text/to_markdown.hpp>

#include <string>

using namespace prism;
using namespace prism::text;
namespace keys = input::keys;
namespace mods = input::mods;

namespace {
struct Rig {
    Document doc;
    Editor ed;
    explicit Rig(std::string_view text) : doc{text}, ed{doc, {.rich = true}} {
        render::DrawList dl;
        ed.view.draw(dl, core::Size{core::Width{600}, core::Height{300}}, theme);
    }
    void key(int32_t k, uint16_t m = 0) { ed.handle(input::KeyPress{k, m}, nullptr); }
    void type(std::string_view s) {
        for (char c : s) ed.handle(input::TextInput{std::string(1, c)}, nullptr);
    }
    static inline const ui::Theme theme{};
};

bool styled(const Block& b, Style s, std::size_t from, std::size_t to) {
    for (const auto& sp : b.spans)
        if (sp.style == s && sp.begin <= from && sp.end >= to) return true;
    return false;
}
} // namespace

TEST_CASE("Ctrl+B makes the selection bold, and again takes it off") {
    Rig r{"make this bold"};
    r.ed.set_caret({0, 5});
    r.ed.set_caret({0, 9}, true);
    r.key(keys::b, mods::ctrl);
    CHECK(styled(r.doc[0], Style::strong, 5, 9));
    r.key(keys::b, mods::ctrl);
    CHECK(r.doc[0].spans.empty());
}

TEST_CASE("with no selection, Ctrl+I styles the word at the caret") {
    Rig r{"one two three"};
    r.ed.set_caret({0, 5});
    r.key(keys::i, mods::ctrl);
    CHECK(styled(r.doc[0], Style::emphasis, 4, 7));
}

TEST_CASE("typing Markdown at a line's start turns it into that kind of block") {
    Rig r{""};
    r.type("## Title");
    CHECK(r.doc[0].kind == BlockKind::heading);
    CHECK(r.doc[0].level == 2);
    CHECK(r.doc[0].text == "Title");
    r.key(keys::enter);
    CHECK(r.doc[1].kind == BlockKind::paragraph);   // after a heading: a paragraph
    r.type("- first");
    CHECK(r.doc[1].kind == BlockKind::bullet);
    CHECK(r.doc[1].text == "first");
    r.key(keys::enter);
    CHECK(r.doc[2].kind == BlockKind::bullet);      // the list goes on
    r.key(keys::enter);
    CHECK(r.doc[2].kind == BlockKind::paragraph);   // Enter on an empty item leaves it
    CHECK(r.doc.size() == 3);
    r.type("> quoted");
    CHECK(r.doc[2].kind == BlockKind::quote);
}

TEST_CASE("numbered items count themselves") {
    Rig r{""};
    r.type("1. one");
    r.key(keys::enter);
    r.type("two");
    CHECK(r.doc[0].kind == BlockKind::ordered);
    CHECK(r.doc[0].info == "1.");
    CHECK(r.doc[1].kind == BlockKind::ordered);
    CHECK(r.doc[1].info == "2.");
}

TEST_CASE("Backspace at the start of a list item or heading makes it a paragraph again") {
    Rig r{""};
    r.type("# Big");
    r.ed.set_caret({0, 0});
    r.key(keys::backspace);
    CHECK(r.doc[0].kind == BlockKind::paragraph);
    CHECK(r.doc[0].text == "Big");
}

TEST_CASE("without rich, Markdown typed stays text and Ctrl+B does nothing") {
    Document doc{""};
    Editor ed{doc};
    for (char c : std::string_view{"# x"}) ed.handle(input::TextInput{std::string(1, c)}, nullptr);
    CHECK(doc[0].kind == BlockKind::paragraph);
    CHECK(doc[0].text == "# x");
}

TEST_CASE("a document written as Markdown: blocks and spans") {
    Document doc{std::vector<Block>{
        {.text = "Title", .kind = BlockKind::heading, .level = 1},
        {.text = "some bold and a link", .spans = {{5, 9, Style::strong}, {16, 20, Style::link, "https://x.org"}}},
        {.text = "item", .kind = BlockKind::bullet, .info = "\xE2\x80\xA2"},
        {.text = "int x;", .kind = BlockKind::code, .info = "cpp"},
        {.text = "said", .kind = BlockKind::quote},
    }};
    CHECK(to_markdown(doc) ==
          "# Title\n\nsome **bold** and a [link](https://x.org)\n\n- item\n\n```cpp\nint x;\n```\n\n> said\n");
}

#ifdef PRISM_HAS_MARKDOWN
#include <prism/text/markdown.hpp>
TEST_CASE("Markdown read and written back reads the same") {
    const std::string source = "# Title\n\nA *word*, `code` and ~~gone~~.\n\n1. one\n2. two\n\n```py\nx = 1\n```\n";
    auto doc = markdown(source);
    auto again = markdown(to_markdown(doc));
    REQUIRE(again.size() == doc.size());
    for (std::size_t i = 0; i < doc.size(); ++i) {
        CHECK(again[i].text == doc[i].text);
        CHECK(again[i].kind == doc[i].kind);
        CHECK(again[i].spans == doc[i].spans);
    }
}
#endif
