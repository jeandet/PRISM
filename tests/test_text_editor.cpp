// text::Editor: a caret and a selection on a Document, moved and edited by keys, text, pastes and
// the mouse. The fake measurer makes positions exact: 10 px a byte, lines 21 px tall, no padding.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/editor.hpp>

#include <string>

namespace doctest {
template <>
struct StringMaker<prism::text::Position> {
    static String convert(const prism::text::Position& p) {
        return ("{" + std::to_string(p.block) + ", " + std::to_string(p.byte) + "}").c_str();
    }
};
} // namespace doctest

using namespace prism;
using namespace prism::text;
using input::KeyPress;
namespace keys = input::keys;
namespace mods = input::mods;

namespace {
struct Fixed : ui::TextMeasurer {
    float width(std::string_view s, float, render::FontStyle) const override { return 10.f * static_cast<float>(s.size()); }
    float line_height(float, render::FontStyle) const override { return 21.f; }
};
const Fixed fixed;
ui::Theme make_theme() {
    ui::Theme t;
    t.measurer = &fixed;
    return t;
}
const ui::Theme theme = make_theme();

struct Copied : ui::Clipboard {
    mutable std::string text;
    void set_text(std::string utf8) const override { text = std::move(utf8); }
};

// An editor drawn once in a 400 x 105 view (five lines), so its geometry is known.
struct Rig {
    Document doc;
    Editor ed;
    Copied clip;
    explicit Rig(std::string_view text, EditorOptions o = {.view = {.family = render::FontRole::Mono, .padding = 0}})
        : doc{text}, ed{doc, o} { draw(); }
    void draw() {
        render::DrawList dl;
        ed.view.draw(dl, core::Size{core::Width{400}, core::Height{105}}, theme);
    }
    void key(int32_t k, uint16_t m = 0) { ed.handle(KeyPress{k, m}, &clip); draw(); }
    void type(std::string_view s) { ed.handle(input::TextInput{std::string(s)}, &clip); draw(); }
    void click(float x, float y, uint8_t clicks = 1, uint16_t m = 0) {
        const core::Point p{core::X{x}, core::Y{y}};
        ed.handle(input::MouseButton{p, input::buttons::left, true, m, clicks}, &clip);
        ed.handle(input::MouseButton{p, input::buttons::left, false, m, clicks}, &clip);
        draw();
    }
};
} // namespace

TEST_CASE("typing inserts at the caret, which follows") {
    Rig r{"hello"};
    r.ed.set_caret({0, 5});
    r.type(" world");
    CHECK(r.doc.text() == "hello world");
    CHECK(r.ed.caret() == Position{0, 11});
}

TEST_CASE("Enter splits the line; Backspace at a line's start joins it back") {
    Rig r{"abcd"};
    r.ed.set_caret({0, 2});
    r.key(keys::enter);
    CHECK(r.doc.text() == "ab\ncd");
    CHECK(r.ed.caret() == Position{1, 0});
    r.key(keys::backspace);
    CHECK(r.doc.text() == "abcd");
    CHECK(r.ed.caret() == Position{0, 2});
}

TEST_CASE("left and right cross line ends, and step over a whole character") {
    Rig r{"ab\ne\xCC\x81x"};   // é as e + combining accent: one character, three bytes
    r.ed.set_caret({0, 2});
    r.key(keys::right);
    CHECK(r.ed.caret() == Position{1, 0});
    r.key(keys::right);
    CHECK(r.ed.caret() == Position{1, 3});
    r.key(keys::left);
    r.key(keys::left);
    CHECK(r.ed.caret() == Position{0, 2});
}

TEST_CASE("Shift selects; typing replaces the selection; Delete erases it") {
    Rig r{"one two"};
    r.ed.set_caret({0, 0});
    for (int i = 0; i < 3; ++i) r.key(keys::right, mods::shift);
    CHECK(r.ed.selected_text() == "one");
    r.type("1");
    CHECK(r.doc.text() == "1 two");
    r.key(keys::end, mods::shift);
    r.key(keys::home);
    r.key(keys::end, mods::shift);
    r.key(keys::delete_);
    CHECK(r.doc.text().empty());
}

TEST_CASE("Ctrl+Left and Ctrl+Backspace work by words") {
    Rig r{"alpha beta gamma"};
    r.ed.set_caret({0, 16});
    r.key(keys::left, mods::ctrl);
    CHECK(r.ed.caret() == Position{0, 11});
    r.key(keys::backspace, mods::ctrl);
    CHECK(r.doc.text() == "alpha gamma");
}

TEST_CASE("up and down keep the column, even through a shorter line") {
    Rig r{"abcdef\nab\nabcdef"};
    r.ed.set_caret({0, 5});
    r.key(keys::down);
    CHECK(r.ed.caret() == Position{1, 2});
    r.key(keys::down);
    CHECK(r.ed.caret() == Position{2, 5});
    r.key(keys::up);
    r.key(keys::up);
    CHECK(r.ed.caret() == Position{0, 5});
}

TEST_CASE("Home and End go to the visual line's ends; Ctrl goes to the document's") {
    Rig r{"0123456789 0123456789 0123456789 0123456789 0123456789\nlast"};   // wraps at 40 chars
    r.ed.set_caret({0, 3});
    r.key(keys::end);
    CHECK(r.ed.caret().block == 0);
    CHECK(r.ed.caret().byte < 40);
    r.key(keys::end, mods::ctrl);
    CHECK(r.ed.caret() == Position{1, 4});
    r.key(keys::home, mods::ctrl);
    CHECK(r.ed.caret() == Position{0, 0});
}

TEST_CASE("copy, cut and paste go through the clipboard and the Paste event") {
    Rig r{"line one\nline two"};
    r.key(keys::a, mods::ctrl);
    r.key(keys::c, mods::ctrl);
    CHECK(r.clip.text == "line one\nline two");
    r.key(keys::x, mods::ctrl);
    CHECK(r.doc.text().empty());
    r.ed.handle(input::Paste{"a\nb"}, &r.clip);
    CHECK(r.doc.text() == "a\nb");
    CHECK(r.ed.caret() == Position{1, 1});
}

TEST_CASE("undo and redo: a word typed is one step") {
    Rig r{""};
    r.type("a");
    r.type("b");
    r.type(" ");
    r.type("c");
    r.key(keys::z, mods::ctrl);
    CHECK(r.doc.text() == "ab ");
    r.key(keys::z, mods::ctrl);
    CHECK(r.doc.text().empty());
    r.key(keys::y, mods::ctrl);
    CHECK(r.doc.text() == "ab ");
}

TEST_CASE("undo and redo put the caret at the change, wherever it was") {
    Rig r{"hello world"};
    r.ed.set_caret({0, 5});
    r.type(" big");                 // "hello big world"
    r.key(keys::home);              // the caret goes away from the change
    r.key(keys::z, mods::ctrl);
    CHECK(r.doc.text() == "hello world");
    CHECK(r.ed.caret() == Position{0, 5});
    r.key(keys::home);
    r.key(keys::y, mods::ctrl);
    CHECK(r.ed.caret() == Position{0, 9});   // after what came back
    r.ed.set_caret({0, 15});
    r.key(keys::backspace, mods::ctrl);      // "hello big " (world gone)
    r.key(keys::home);
    r.key(keys::z, mods::ctrl);
    CHECK(r.ed.caret() == Position{0, 15});  // after the word put back
}

TEST_CASE("a click puts the caret where it lands; a drag selects; a double click takes a word") {
    Rig r{"first line\nsecond line"};
    r.click(32, 25);                              // line 2 (y 21..42), between "sec" and "ond"
    CHECK(r.ed.caret() == Position{1, 3});
    const core::Point to{core::X{60}, core::Y{25}};
    r.ed.handle(input::MouseButton{core::Point{core::X{0}, core::Y{5}}, input::buttons::left, true}, &r.clip);
    r.ed.handle(input::MouseMove{to, input::buttons::left_mask}, &r.clip);
    r.ed.handle(input::MouseButton{to, input::buttons::left, false}, &r.clip);
    CHECK(r.ed.selected_text() == "first line\nsecond");
    r.click(72, 25, 2);
    CHECK(r.ed.selected_text() == "line");
}

TEST_CASE("moving the caret below the view scrolls it into sight") {
    Rig r{"1\n2\n3\n4\n5\n6\n7\n8\n9\n10"};
    r.ed.set_caret({0, 0});
    for (int i = 0; i < 8; ++i) r.key(keys::down);
    CHECK(r.ed.view.scroll.raw() > 0.f);
    CHECK(r.ed.view.scroll.raw() + 105.f >= 9 * 21.f);
}

TEST_CASE("a new editor on a long document starts at the top, its first line in sight") {
    Rig r{"1\n2\n3\n4\n5\n6\n7\n8\n9\n10"};
    CHECK(r.ed.view.scroll.raw() == 0.f);
    CHECK(r.ed.view.first_drawn() == 0);
}

TEST_CASE("read-only: no typing, but selecting and copying still work") {
    Rig r{"fixed text", {.view = {.family = render::FontRole::Mono, .padding = 0}, .read_only = true}};
    r.type("x");
    r.key(keys::backspace);
    CHECK(r.doc.text() == "fixed text");
    r.key(keys::a, mods::ctrl);
    r.key(keys::c, mods::ctrl);
    CHECK(r.clip.text == "fixed text");
}

TEST_CASE("with tab_inserts, Tab types the indent and Shift+Tab removes it; auto_indent keeps it on Enter") {
    Rig r{"x", {.view = {.family = render::FontRole::Mono, .padding = 0}, .tab_inserts = true, .auto_indent = true}};
    r.ed.set_caret({0, 0});
    r.key(keys::tab);
    CHECK(r.doc.text() == "    x");
    r.key(keys::end);
    r.key(keys::enter);
    CHECK(r.doc.text() == "    x\n    ");
    r.key(keys::tab, mods::shift);
    CHECK(r.doc.text() == "    x\n");
}

TEST_CASE("an edit made elsewhere leaves the caret inside the document") {
    Rig r{"a long line"};
    r.ed.set_caret({0, 11});
    r.doc.assign(Document{"ab"});
    r.type("c");
    CHECK(r.doc.text() == "abc");
}

// --- In a window: vb.text_editor(doc) -------------------------------------------------------

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

namespace {
struct Notes {
    Document doc{"first line\nsecond line"};
    void view(app::ViewBuilder& vb) { vb.text_editor(doc, {.view = {.family = render::FontRole::Mono, .padding = 0}}); }
};

int carets(const render::SceneSnapshot& snap, const ui::Theme& t) {
    int n = 0;
    for (const auto& dl : snap.draw_lists)
        for (const auto& c : dl->commands)
            if (auto* r = std::get_if<render::FilledRect>(&c); r && r->color == t.cursor && r->rect.extent.w.raw() == 2.f) ++n;
    return n;
}
} // namespace

TEST_CASE("vb.text_editor: a click focuses it, keys and text edit the document, the caret shows when focused") {
    Notes notes;
    app::WidgetTree tree{notes};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    CHECK(carets(*snap, tree.theme()) == 0);

    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{core::Point{core::X{60}, core::Y{25}}, input::buttons::left, true});
    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{core::Point{core::X{60}, core::Y{25}}, input::buttons::left, false});
    app::widget_detail::route_event(tree, snap.get(), input::TextInput{"X"});
    app::widget_detail::route_event(tree, snap.get(), KeyPress{keys::enter, 0});
    CHECK(notes.doc.text() == "first line\nsecondX\n line");
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK(carets(*snap, tree.theme()) == 1);

    app::widget_detail::route_event(tree, snap.get(), KeyPress{keys::z, mods::ctrl});
    CHECK(notes.doc.text() == "first line\nsecondX line");
}
