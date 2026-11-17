// Completion: candidates ranked against the word being typed, a list under the caret in the
// editor, and the one taken put in place of the word, one undo step. The fake measurer makes
// positions exact: 10 px a byte, lines 21 px tall, no padding.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/text/complete.hpp>
#include <prism/text/editor.hpp>

#include <algorithm>
#include <string>

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

std::vector<std::string> labels(const std::vector<Completion>& items) {
    std::vector<std::string> out;
    for (const auto& c : items) out.push_back(c.label);
    return out;
}

std::vector<Completion> from(std::vector<std::string> words) {
    std::vector<Completion> out;
    for (auto& w : words) out.push_back({.label = std::move(w)});
    return out;
}

// An editor drawn once in a 400 x 105 view (five lines), completing from `words`.
struct Rig {
    Document doc;
    Editor ed;
    explicit Rig(std::string_view text, std::vector<std::string> words, bool read_only = false)
        : doc{text}, ed{doc, {.view = {.family = render::FontRole::Mono, .padding = 0}, .read_only = read_only,
                              .complete = from_list(std::move(words))}} {
        ed.set_caret(doc.end());
        draw();
    }
    render::DrawList popup;
    void draw() {
        render::DrawList dl;
        ed.view.draw(dl, core::Size{core::Width{400}, core::Height{105}}, theme);
        popup.clear();
        ed.draw_completions(popup, theme);
    }
    void key(int32_t k, uint16_t m = 0) { ed.handle(KeyPress{k, m}, nullptr); draw(); }
    void type(std::string_view s) { ed.handle(input::TextInput{std::string(s)}, nullptr); draw(); }
    std::vector<std::string> shown() const { return ed.completing() ? labels(ed.completing()->items) : std::vector<std::string>{}; }
};
} // namespace

TEST_CASE("fuzzy_score: every typed character in order, ignoring case") {
    CHECK(fuzzy_score("gw", "get_widget"));
    CHECK(fuzzy_score("GW", "get_widget"));
    CHECK_FALSE(fuzzy_score("dw", "get_widget"));
    CHECK_FALSE(fuzzy_score("x", "get_widget"));
    CHECK(fuzzy_score("", "anything") == 0);
}

TEST_CASE("fuzzy_score: a prefix beats a word start, which beats the middle of a word") {
    CHECK(*fuzzy_score("wid", "widget") > *fuzzy_score("wid", "get_widget"));
    CHECK(*fuzzy_score("wid", "get_widget") > *fuzzy_score("wid", "rewide"));
    CHECK(*fuzzy_score("gw", "getWidget") > *fuzzy_score("gw", "glow"));
    CHECK(*fuzzy_score("Get", "Get") > *fuzzy_score("Get", "get"));
}

TEST_CASE("fuzzy_score: a run at the start beats the same letters spread over words") {
    CHECK(*fuzzy_score("st", "std") > *fuzzy_score("st", "size_t"));
}

TEST_CASE("rank: the matches, best first, ties shorter first; the word typed whole left out") {
    auto ranked = rank(from({"rewide", "widget_tree", "get_widget", "widget", "button", "wid"}), "wid");
    CHECK(labels(ranked) == std::vector<std::string>{"widget", "widget_tree", "get_widget", "rewide"});
    CHECK(rank(from({"b", "a", "c"}), "").size() == 3);
    CHECK(labels(rank(from({"b", "a", "c"}), "")) == std::vector<std::string>{"a", "b", "c"});
    CHECK(rank(from({"a", "b", "c"}), "", 2).size() == 2);
}

TEST_CASE("document_words: the document's words but not the one being typed, and the language's") {
    Document doc{"int counter = 0;\ncounter += cou"};
    const auto all = document_words(&language("cpp"))(doc, doc.end());
    auto has = [&](std::string_view w, std::string_view detail = {}) {
        return std::ranges::any_of(all, [&](const Completion& c) { return c.label == w && c.detail == detail; });
    };
    CHECK(has("counter"));
    CHECK_FALSE(has("cou"));
    CHECK(has("return", "keyword"));
    CHECK(has("int", "type"));
    CHECK_FALSE(has("int"));   // a type once, as a type, not again as one of the document's words
    CHECK(std::ranges::count_if(all, [](const Completion& c) { return c.label == "counter"; }) == 1);
}

TEST_CASE("typing a word opens the list; Enter puts the best one in its place, one undo step") {
    Rig r{"x = ", {"print", "private", "sprint", "pass"}};
    r.type("p");
    r.type("r");
    r.type("i");
    CHECK(r.shown() == std::vector<std::string>{"print", "private", "sprint"});
    r.key(keys::enter);
    CHECK(r.doc.text() == "x = print");
    CHECK_FALSE(r.ed.completing());
    CHECK(r.ed.caret() == Position{0, 9});
    r.key(keys::z, mods::ctrl);
    CHECK(r.doc.text() == "x = pri");
}

TEST_CASE("Down picks the next one and Tab takes it; Escape closes the list and keeps the text") {
    Rig r{"", {"print", "private"}};
    r.type("pr");
    r.key(keys::down);
    CHECK(r.ed.completing()->selected == 1);
    r.key(keys::tab);
    CHECK(r.doc.text() == "private");

    r.type(" pr");
    REQUIRE(r.ed.completing());
    r.key(keys::escape);
    CHECK_FALSE(r.ed.completing());
    CHECK(r.doc.text() == "private pr");
}

TEST_CASE("a space or a move closes the list; Backspace narrows it again, and closes it on an empty word") {
    Rig r{"", {"print", "private", "pass"}};
    r.type("pr");
    r.type(" ");
    CHECK_FALSE(r.ed.completing());

    r.type("pri");
    r.key(keys::left);
    CHECK_FALSE(r.ed.completing());

    r.key(keys::end);
    r.type("v");
    CHECK(r.shown() == std::vector<std::string>{"private"});
    r.key(keys::backspace);
    CHECK(r.shown() == std::vector<std::string>{"print", "private"});
    r.key(keys::backspace);
    r.key(keys::backspace);
    r.key(keys::backspace);
    CHECK_FALSE(r.ed.completing());
}

TEST_CASE("Ctrl+Space opens the list with nothing typed; nothing matching keeps it closed") {
    Rig r{"x ", {"b", "a"}};
    r.key(keys::space, mods::ctrl);
    CHECK(r.shown() == std::vector<std::string>{"a", "b"});
    r.key(keys::escape);
    r.type("zz");
    CHECK_FALSE(r.ed.completing());
}

TEST_CASE("a read-only editor never completes") {
    Rig r{"", {"print"}, true};
    r.key(keys::space, mods::ctrl);
    CHECK_FALSE(r.ed.completing());
}

TEST_CASE("the list is drawn under the caret's line, the picked row highlighted, and a click takes a row") {
    Rig r{"a\npr", {"print", "private"}};
    r.type("i");
    const auto bounds = r.popup.bounding_box();
    CHECK(bounds.origin.y.raw() >= 42.f);   // under the second line (21 px each)
    CHECK(bounds.origin.x.raw() <= 10.f);
    bool highlighted = false;
    for (const auto& c : r.popup.commands)
        if (auto* f = std::get_if<render::FilledRect>(&c); f && f->color == theme.popup_highlight) highlighted = true;
    CHECK(highlighted);

    // The second row, a few px into it.
    const float row = r.ed.completion_row_height();
    const core::Point second{core::X{bounds.origin.x.raw() + 20.f}, core::Y{bounds.origin.y.raw() + 1.5f * row + 4.f}};
    r.ed.handle(input::MouseButton{second, input::buttons::left, true}, nullptr);
    r.ed.handle(input::MouseButton{second, input::buttons::left, false}, nullptr);
    CHECK(r.doc.text() == "a\nprivate");
}

namespace {
struct Source {
    Document doc{"int counter = 0;\n"};
    void view(app::ViewBuilder& vb) { vb.code_editor(doc, "cpp"); }
};

bool overlay_has(const render::SceneSnapshot& snap, std::string_view text) {
    return std::ranges::any_of(snap.overlay.commands, [&](const render::DrawCmd& c) {
        auto* t = std::get_if<render::TextCmd>(&c);
        return t && t->text == text;
    });
}
} // namespace

TEST_CASE("vb.code_editor completes the file's words and the language's keywords, over the page") {
    Source src;
    app::WidgetTree tree{src};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    auto route = [&](const input::InputEvent& ev) { app::widget_detail::route_event(tree, snap.get(), ev); };
    route(input::MouseButton{core::Point{core::X{300}, core::Y{40}}, input::buttons::left, true});
    route(input::MouseButton{core::Point{core::X{300}, core::Y{40}}, input::buttons::left, false});
    route(KeyPress{keys::end, mods::ctrl});
    route(input::TextInput{"co"});
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK(overlay_has(*snap, "counter"));
    CHECK(overlay_has(*snap, "const"));
    route(input::TextInput{"u"});
    route(KeyPress{keys::tab, 0});   // takes it, rather than indenting
    CHECK(src.doc.text() == "int counter = 0;\ncounter");
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK_FALSE(overlay_has(*snap, "const"));
}

TEST_CASE("the list closes when the editor loses the focus") {
    Source src;
    app::WidgetTree tree{src};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    auto route = [&](const input::InputEvent& ev) { app::widget_detail::route_event(tree, snap.get(), ev); };
    route(input::MouseButton{core::Point{core::X{300}, core::Y{40}}, input::buttons::left, true});
    route(input::MouseButton{core::Point{core::X{300}, core::Y{40}}, input::buttons::left, false});
    route(input::TextInput{"co"});
    snap = tree.build_snapshot(400, 300, ++version);
    REQUIRE(overlay_has(*snap, "const"));
    tree.clear_focus();
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK_FALSE(overlay_has(*snap, "const"));
}
