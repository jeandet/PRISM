// A view with a highlighter, line numbers and the current line; an editor matching brackets; and
// vb.code_editor(doc, "cpp") putting them together.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/text/editor.hpp>
#include <prism/text/highlight.hpp>

#include <string>

using namespace prism;
using namespace prism::text;

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
const core::Size window{core::Width{400}, core::Height{210}};   // ten lines

// The C++ rules, counting the lines it is asked to highlight.
struct Counting : Highlighter {
    mutable int lines = 0;
    State highlight(std::string_view line, State before, std::vector<Span>& out) const override {
        ++lines;
        return language("cpp").highlight(line, before, out);
    }
};

std::string many_lines(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += "int v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    return s;
}

const render::TextCmd* text_cmd(const render::DrawList& dl, std::string_view text) {
    for (const auto& c : dl.commands)
        if (auto* t = std::get_if<render::TextCmd>(&c); t && t->text == text) return t;
    return nullptr;
}
} // namespace

TEST_CASE("a highlighted view draws keywords and types in their colors") {
    Document doc{"return 0;\nint x;"};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0, .highlighter = &language("cpp")}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    REQUIRE(text_cmd(dl, "return"));
    CHECK(text_cmd(dl, "return")->color == theme.syntax_keyword);
    REQUIRE(text_cmd(dl, "int"));
    CHECK(text_cmd(dl, "int")->color == theme.syntax_type);
}

TEST_CASE("typing on a line highlights that line again, not the ones below it") {
    Counting counting;
    Document doc{many_lines(2000)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0, .highlighter = &counting}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    counting.lines = 0;
    doc.insert({3, 0}, "  ");
    dl.clear();
    view.draw(dl, window, theme);
    CHECK(counting.lines <= 4);
}

TEST_CASE("opening a block comment turns the lines below into comment, closing it turns them back") {
    Document doc{"int a;\nint b;\nint c;"};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0, .highlighter = &language("cpp")}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    doc.insert({0, 6}, " /*");
    dl.clear();
    view.draw(dl, window, theme);
    REQUIRE(text_cmd(dl, "int c;"));
    CHECK(text_cmd(dl, "int c;")->color == theme.syntax_comment);
    doc.insert({1, 6}, " */");
    dl.clear();
    view.draw(dl, window, theme);
    REQUIRE(text_cmd(dl, "int"));
    CHECK(text_cmd(dl, "int")->color == theme.syntax_type);
}

TEST_CASE("line numbers sit in a gutter; the text starts after it") {
    Document doc{"one\ntwo\nthree"};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0, .line_numbers = true}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    REQUIRE(text_cmd(dl, "1"));
    REQUIRE(text_cmd(dl, "3"));
    REQUIRE(text_cmd(dl, "one"));
    CHECK(text_cmd(dl, "one")->origin.x.raw() > text_cmd(dl, "1")->origin.x.raw() + 10.f);
    CHECK(view.hit(core::Point{core::X{text_cmd(dl, "one")->origin.x.raw() + 11.f}, core::Y{5}}) == Position{0, 1});
}

TEST_CASE("the caret next to a bracket highlights it and its match, across lines") {
    Document doc{"f(a, {b,\n c});"};
    Editor ed{doc, {.view = {.family = render::FontRole::Mono, .padding = 0}, .match_brackets = true}};
    render::DrawList dl;
    ed.view.draw(dl, window, theme);
    ed.set_caret({0, 5});   // before {
    REQUIRE(ed.view.marks);
    REQUIRE(ed.view.marks->highlights.size() == 2);
    CHECK(ed.view.marks->highlights[0].begin == Position{0, 5});
    CHECK(ed.view.marks->highlights[1].begin == Position{1, 2});
    ed.set_caret({0, 3});   // between a and ,
    CHECK(ed.view.marks->highlights.empty());
}

TEST_CASE("review: a bracket before the caret is matched even when the one at the caret has no match") {
    Document doc{"(a)("};
    Editor ed{doc, {.view = {.family = render::FontRole::Mono, .padding = 0}, .match_brackets = true}};
    render::DrawList dl;
    ed.view.draw(dl, window, theme);
    ed.set_caret({0, 3});   // after ")", before the unmatched "("
    REQUIRE(ed.view.marks);
    REQUIRE(ed.view.marks->highlights.size() == 2);
    CHECK(ed.view.marks->highlights[0].begin == Position{0, 2});
    CHECK(ed.view.marks->highlights[1].begin == Position{0, 0});
}

TEST_CASE("review: a composition stays shown when the document changes meanwhile") {
    Document doc{"ab"};
    Editor ed{doc};
    ed.handle(input::Composition{"x", 1, 0}, nullptr);
    doc.insert({0, 2}, "c");
    REQUIRE(ed.view.marks);
    CHECK(ed.view.marks->preedit == "x");
}

namespace {
struct Source {
    Document doc{"int main() {\n    return 0;\n}"};
    void view(app::ViewBuilder& vb) { vb.code_editor(doc, "cpp"); }
};
} // namespace

TEST_CASE("vb.code_editor(doc, \"cpp\"): numbered, highlighted, and Tab indents") {
    Source src;
    app::WidgetTree tree{src};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    bool numbered = false, keyword = false;
    for (const auto& dl : snap->draw_lists) {
        if (text_cmd(*dl, "3")) numbered = true;
        if (auto* t = text_cmd(*dl, "return"); t && t->color == tree.theme().syntax_keyword) keyword = true;
    }
    CHECK(numbered);
    CHECK(keyword);
    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{core::Point{core::X{200}, core::Y{30}}, input::buttons::left, true});
    app::widget_detail::route_event(tree, snap.get(), input::MouseButton{core::Point{core::X{200}, core::Y{30}}, input::buttons::left, false});
    app::widget_detail::route_event(tree, snap.get(), input::KeyPress{input::keys::home, 0});
    app::widget_detail::route_event(tree, snap.get(), input::KeyPress{input::keys::tab, 0});
    CHECK(src.doc[1].text == "        return 0;");
}

namespace {
struct Marked {
    Document doc{"select * from trips"};
    core::Field<std::vector<Range>> errors{};
    void view(app::ViewBuilder& vb) { vb.code_editor(doc, "sql").highlights(errors); }
};

int match_rects(const render::SceneSnapshot& snap, const ui::Theme& t) {
    int n = 0;
    for (const auto& dl : snap.draw_lists)
        for (const auto& c : dl->commands)
            if (auto* r = std::get_if<render::FilledRect>(&c); r && r->color == t.text_match) ++n;
    return n;
}
} // namespace

TEST_CASE(".highlights(field): the app's ranges are drawn, focused or not, and follow the field") {
    Marked m;
    app::WidgetTree tree{m};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    CHECK(match_rects(*snap, tree.theme()) == 0);
    m.errors.set({{{0, 9}, {0, 13}}});   // "from"
    snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    CHECK(match_rects(*snap, tree.theme()) == 1);
    m.errors.set({});
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK(match_rects(*snap, tree.theme()) == 0);
}

TEST_CASE("without wrapping, a long line scrolls sideways; the gutter stays put") {
    Document doc{"short\n" + std::string(200, 'x')};
    View view{doc, {.family = render::FontRole::Mono, .wrap = false, .padding = 0, .line_numbers = true}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    const float number_x = text_cmd(dl, "1")->origin.x.raw();
    const float text_x = text_cmd(dl, "short")->origin.x.raw();
    view.scroll_x = core::DX{150};
    dl.clear();
    view.draw(dl, window, theme);
    CHECK(text_cmd(dl, "1")->origin.x.raw() == doctest::Approx(number_x));
    CHECK(text_cmd(dl, "short")->origin.x.raw() == doctest::Approx(text_x - 150));
    CHECK(text_cmd(dl, "2")->origin.y.raw() == doctest::Approx(21.f));   // lines stay where they are
    CHECK(text_cmd(dl, "short")->origin.y.raw() == doctest::Approx(0.f));
    view.scroll_x = core::DX{1e6f};
    dl.clear();
    view.draw(dl, window, theme);
    CHECK(view.scroll_x.raw() == doctest::Approx(2000.f - (400.f - (text_x - 0.f))));   // held at the end
}

TEST_CASE("the caret is kept in sight sideways, and a sideways swipe scrolls") {
    Document doc{std::string(200, 'x')};
    Editor ed{doc, {.view = {.family = render::FontRole::Mono, .wrap = false, .padding = 0}}};
    render::DrawList dl;
    ed.view.draw(dl, window, theme);
    ed.handle(input::KeyPress{input::keys::end, 0}, nullptr);
    CHECK(ed.view.scroll_x.raw() > 1500.f);
    ed.handle(input::KeyPress{input::keys::home, 0}, nullptr);
    CHECK(ed.view.scroll_x.raw() == 0.f);
    ed.handle(input::MouseScroll{core::Point{core::X{50}, core::Y{5}}, core::DX{-10}, core::DY{0}}, nullptr);
    CHECK(ed.view.scroll_x.raw() > 0.f);
    // a click lands where the text is now drawn
    dl.clear();
    ed.view.draw(dl, window, theme);
    const float scrolled = ed.view.scroll_x.raw();
    CHECK(ed.view.hit(core::Point{core::X{20}, core::Y{5}}).byte == static_cast<std::size_t>((20.f + scrolled) / 10.f + 0.5f));
}

TEST_CASE("highlighting goes on ahead in idle time; a jump to the end then highlights only what is in sight") {
    Counting counting;
    Document doc{many_lines(20000)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0, .highlighter = &counting}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    int batches = 0;
    while (view.highlight_ahead(5000)) ++batches;
    CHECK(batches == 3);                 // 20 000 lines, 5 000 a batch: the fourth finishes
    CHECK_FALSE(view.highlight_ahead(5000));
    counting.lines = 0;
    view.scroll = core::DY{1e9f};
    dl.clear();
    view.draw(dl, window, theme);
    CHECK(counting.lines < 40);
}
