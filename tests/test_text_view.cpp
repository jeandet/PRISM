// text::View: a document drawn into a rectangle, laying out only the blocks in sight, with a height
// map so it can scroll a document far taller than the window (doc/design/text-stack.md).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/text/view.hpp>

#include <string>
#include <variant>

using namespace prism;
using namespace prism::text;

namespace {
struct Fixed : ui::TextMeasurer {
    float width(std::string_view s, float size, render::FontStyle) const override {
        return static_cast<float>(s.size()) * 10.f * size / 14.f;
    }
    float line_height(float size, render::FontStyle) const override { return 1.5f * size; }
};
const Fixed fixed;

ui::Theme with_fixed_measurer() {
    ui::Theme t;
    t.measurer = &fixed;
    return t;
}
const ui::Theme theme = with_fixed_measurer();

std::string numbered_lines(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += "line " + std::to_string(i) + (i + 1 < n ? "\n" : "");
    return s;
}

bool draws_text(const render::DrawList& dl, std::string_view text) {
    for (const auto& c : dl.commands)
        if (auto* t = std::get_if<render::TextCmd>(&c); t && t->text == text) return true;
    return false;
}

const core::Size window{core::Width{400}, core::Height{200}};
} // namespace

TEST_CASE("a long document lays out only what is in sight") {
    Document doc{numbered_lines(10000)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    CHECK(draws_text(dl, "line 0"));
    CHECK_FALSE(draws_text(dl, "line 50"));
    CHECK(view.laid_out() < 20);
    CHECK(view.content_h.raw() == doctest::Approx(10000 * 21.f));
}

TEST_CASE("scrolling shows the blocks at that height, found through the height map") {
    Document doc{numbered_lines(10000)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0}};
    view.scroll = core::DY{21.f * 5000};
    render::DrawList dl;
    view.draw(dl, window, theme);
    CHECK(view.first_drawn() == 5000);
    CHECK(draws_text(dl, "line 5000"));
    CHECK_FALSE(draws_text(dl, "line 0"));
}

TEST_CASE("scrolling past the end is held at the end") {
    Document doc{numbered_lines(100)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0}};
    view.scroll = core::DY{1e9f};
    render::DrawList dl;
    view.draw(dl, window, theme);
    CHECK(view.scroll.raw() == doctest::Approx(100 * 21.f - 200.f));
    CHECK(draws_text(dl, "line 99"));
}

TEST_CASE("an edit shows at the next draw; only the changed block is laid out again") {
    Document doc{numbered_lines(50)};
    View view{doc, {.family = render::FontRole::Mono, .padding = 0}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    const auto before = view.layouts_made();
    doc.insert({1, 0}, "new\n");
    dl.clear();
    view.draw(dl, window, theme);
    CHECK(draws_text(dl, "new"));
    CHECK(draws_text(dl, "line 1"));
    CHECK(view.layouts_made() - before == 2);   // block 1 became two
    CHECK(view.content_h.raw() == doctest::Approx(51 * 21.f));
}

TEST_CASE("a narrower window wraps again") {
    Document doc{"aaaa bbbb cccc dddd"};
    View view{doc, {.padding = 0}};
    render::DrawList dl;
    view.draw(dl, window, theme);
    CHECK(draws_text(dl, "aaaa bbbb cccc dddd"));
    dl.clear();
    view.draw(dl, {core::Width{100}, core::Height{200}}, theme);
    CHECK(draws_text(dl, "aaaa bbbb "));
}

TEST_CASE("a code block gets its background") {
    Document doc{std::vector<Block>{{.text = "int x;", .kind = BlockKind::code}}};
    View view{doc};
    render::DrawList dl;
    view.draw(dl, window, theme);
    bool background = false;
    for (const auto& c : dl.commands)
        if (auto* r = std::get_if<render::FilledRect>(&c); r && r->color == theme.code_bg) background = true;
    CHECK(background);
}

// --- In a window: vb.text_view(doc) ---------------------------------------------------------

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

namespace {
struct Reader {
    Document doc{numbered_lines(2000)};
    void view(app::ViewBuilder& vb) { vb.text_view(doc, {.family = render::FontRole::Mono}); }
};

bool shows(const render::SceneSnapshot& snap, std::string_view text) {
    for (const auto& dl : snap.draw_lists)
        if (draws_text(*dl, text)) return true;
    return false;
}
} // namespace

TEST_CASE("vb.text_view: the wheel scrolls it, and an edit of the document redraws it") {
    Reader r;
    app::WidgetTree tree{r};
    tree.set_text_measurer(&fixed);
    std::uint64_t version = 0;
    auto snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    CHECK(shows(*snap, "line 0"));
    CHECK_FALSE(shows(*snap, "line 200"));

    const core::Point inside{core::X{100}, core::Y{100}};
    for (int i = 0; i < 40; ++i)
        app::widget_detail::route_event(tree, snap.get(), input::MouseScroll{inside, core::DX{0}, core::DY{-3}});
    snap = tree.build_snapshot(400, 300, ++version);
    tree.clear_dirty();
    CHECK_FALSE(shows(*snap, "line 0"));

    r.doc.insert({0, 0}, "edited ");
    for (int i = 0; i < 40; ++i)
        app::widget_detail::route_event(tree, snap.get(), input::MouseScroll{inside, core::DX{0}, core::DY{3}});
    snap = tree.build_snapshot(400, 300, ++version);
    CHECK(shows(*snap, "edited line 0"));
}
