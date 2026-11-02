#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app;
}
using namespace prism;
using prism::app::widget_detail::route_event;

namespace {
struct Doc {
    Field<Button> below{{"underneath"}};
    Field<Dialog> ask{{.title = "Unsaved changes", .message = "Discard your edits?",
                       .buttons = {"Discard", "Cancel"}}};
    int below_clicks = 0;
    Connection c = below.on_change().connect([this](const Button&) { ++below_clicks; });
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(below);
            vb.dialog(ask);
        });
    }
};

struct Fixture {
    Doc model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(600, 400, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(600, 400, ++v); }
    void show() {
        auto d = model.ask.get();
        d.open = true;
        model.ask.set(d);
        frame();
    }
    void click(float x, float y) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false});
    }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); }
    std::optional<Rect> card() const { return widget_detail::popup_area(tree, *snap); }
    bool is_open() const { return tree.popup_owner() != 0; }
};
}

TEST_CASE("opening a dialog from the model makes it a modal popup centered in the window") {
    Fixture f;
    CHECK_FALSE(f.is_open());
    f.show();
    REQUIRE(f.is_open());
    CHECK(f.tree.popup_node()->popup_modal);
    auto c = f.card();
    REQUIRE(c);
    CHECK(c->origin.x.raw() + c->extent.w.raw() / 2 == doctest::Approx(300.f).epsilon(0.01));
    CHECK(c->origin.y.raw() + c->extent.h.raw() / 2 == doctest::Approx(200.f).epsilon(0.01));
    // the scrim dims the whole window
    auto scrim = std::ranges::any_of(f.snap->overlay.commands, [](const DrawCmd& cmd) {
        auto* r = std::get_if<FilledRect>(&cmd);
        return r && r->rect.extent.w.raw() >= 600.f && r->rect.extent.h.raw() >= 400.f;
    });
    CHECK(scrim);
}

TEST_CASE("a modal dialog swallows clicks outside it and keeps open") {
    Fixture f;
    f.show();
    f.click(20, 10);   // on the button underneath
    CHECK(f.model.below_clicks == 0);
    CHECK(f.is_open());
}

TEST_CASE("Enter chooses the first button; the dialog closes and reports it") {
    Fixture f;
    f.show();
    f.key(keys::enter);
    CHECK_FALSE(f.is_open());
    CHECK_FALSE(f.model.ask.get().open);
    CHECK(f.model.ask.get().chosen == std::optional<size_t>{0});
    CHECK(f.model.ask.get().activations == 1);
    f.frame();
    CHECK(f.snap->overlay.commands.empty());
}

TEST_CASE("Escape dismisses without a choice; Tab moves to the next button") {
    Fixture f;
    f.show();
    f.key(keys::escape);
    CHECK_FALSE(f.is_open());
    CHECK(f.model.ask.get().chosen == std::nullopt);
    CHECK(f.model.ask.get().activations == 1);

    f.show();
    f.key(keys::tab);
    f.key(keys::enter);
    CHECK(f.model.ask.get().chosen == std::optional<size_t>{1});
}

TEST_CASE("clicking a button in the card chooses it") {
    Fixture f;
    f.show();
    auto c = *f.card();
    // buttons sit in the card's bottom row, right-aligned; the last one is "Cancel"
    float y = c.origin.y.raw() + c.extent.h.raw() - 35.f;   // 20 px padding, 30 px buttons
    float x = c.origin.x.raw() + c.extent.w.raw() - 30.f;
    f.click(x, y);
    CHECK(f.model.ask.get().chosen == std::optional<size_t>{1});
    CHECK_FALSE(f.is_open());
}

TEST_CASE("the app can close the dialog by setting open = false") {
    Fixture f;
    f.show();
    auto d = f.model.ask.get();
    d.open = false;
    f.model.ask.set(d);
    f.frame();
    CHECK_FALSE(f.is_open());
    f.click(20, 10);
    CHECK(f.model.below_clicks == 1);
}

TEST_CASE("an open dialog re-centres when the window is resized") {
    Fixture f;
    f.show();
    f.snap = f.tree.build_snapshot(1000, 800, 10);
    auto c = f.card();
    REQUIRE(c);
    CHECK(c->origin.x.raw() + c->extent.w.raw() / 2 == doctest::Approx(500.f).epsilon(0.01));
    CHECK(c->origin.y.raw() + c->extent.h.raw() / 2 == doctest::Approx(400.f).epsilon(0.01));
}

// ---- Findings from the opencode review of 9184924 ----

TEST_CASE("the app closing the dialog frees input immediately, before the next frame") {
    Fixture f;
    f.show();
    auto d = f.model.ask.get();
    d.open = false;
    f.model.ask.set(d);
    f.click(20, 10);   // no frame() in between
    // Routing no longer treats it as modal. The click itself still meets last frame's
    // picture, where the dialog covers the window, like any change shown one frame late.
    CHECK(f.tree.popup_owner() == 0);
    f.frame();
    f.click(20, 10);
    CHECK(f.model.below_clicks == 1);
}

TEST_CASE("a dialog opened while a menu is open takes over") {
    struct WithMenu {
        Field<Menu> file{{.title = "File", .items = {"A", "B"}}};
        Field<Dialog> ask{{.title = "T", .message = "M", .buttons = {"OK", "No"}}};
        void view(WidgetTree::ViewBuilder& vb) { vb.vstack([&] { vb.widget(file); vb.dialog(ask); }); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(600, 400, 1);
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::left, true});
    REQUIRE(tree.popup_owner() != 0);
    auto d = m.ask.get();
    d.open = true;
    m.ask.set(d);
    snap = tree.build_snapshot(600, 400, 2);
    REQUIRE(tree.popup_node());
    CHECK(tree.popup_node()->popup_modal);
    route_event(tree, snap.get(), KeyPress{keys::enter, 0});
    CHECK(m.ask.get().chosen == std::optional<size_t>{0});
    CHECK_FALSE(m.file.get().chosen.has_value());
}

TEST_CASE("a card larger than the window stays put across events") {
    Fixture f;
    f.show();
    f.snap = f.tree.build_snapshot(200, 80, 20);
    auto before = *f.card();
    route_event(f.tree, f.snap.get(), MouseMove{Point{X{5}, Y{5}}});
    f.snap = f.tree.build_snapshot(200, 80, 21);
    CHECK(f.card()->origin == before.origin);
    CHECK(before.origin.x.raw() >= 0.f);
    CHECK(before.origin.y.raw() >= 0.f);
}

TEST_CASE("a multi-line message makes the card taller") {
    Fixture f;
    f.show();
    float one = f.card()->extent.h.raw();
    auto d = f.model.ask.get();
    d.message = "line one\nline two\nline three";
    f.model.ask.set(d);
    f.frame();
    CHECK(f.card()->extent.h.raw() > one + 30.f);
}

TEST_CASE("the highlighted button is clamped when the buttons shrink") {
    Fixture f;
    f.show();
    f.key(keys::tab);   // highlight "Cancel"
    auto d = f.model.ask.get();
    d.buttons = {"OK"};
    f.model.ask.set(d);
    f.frame();
    f.key(keys::enter);
    CHECK(f.model.ask.get().chosen == std::optional<size_t>{0});
}

// --- long messages --------------------------------------------------------------

namespace {
std::string numbered(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += (i ? "\n" : "") + std::string("message line ") + std::to_string(i);
    return s;
}

// Whether the last frame draws `text` in the popup layer (the dialog's message is a text area there).
bool drawn(const Fixture& f, std::string_view text) {
    for (auto& c : f.snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
    return false;
}

int drawn_starting_with(const Fixture& f, std::string_view prefix) {
    int n = 0;
    for (auto& c : f.snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text.starts_with(prefix)) ++n;
    return n;
}

void show_message(Fixture& f, std::string message) {
    auto d = f.model.ask.get();
    d.message = std::move(message);
    d.open = true;
    f.model.ask.set(d);
    f.frame();
}
}

TEST_CASE("a message taller than the window: the card fits and its buttons stay clickable") {
    Fixture f;
    show_message(f, numbered(100));
    auto c = f.card();
    REQUIRE(c);
    CHECK(c->origin.y.raw() >= 0.f);
    CHECK(c->origin.y.raw() + c->extent.h.raw() <= 400.f);
    // The last button sits along the card's bottom edge: a click there chooses it.
    float x = c->origin.x.raw() + c->extent.w.raw() - 20.f - 10.f;
    float y = c->origin.y.raw() + c->extent.h.raw() - 20.f - 15.f;
    f.click(x, y);
    CHECK(f.model.ask.get().chosen == std::optional<std::size_t>{1});
}

TEST_CASE("the wheel and the arrow keys scroll a long message; the end is reachable") {
    Fixture f;
    show_message(f, numbered(100));
    REQUIRE(drawn(f, "message line 0"));
    REQUIRE_FALSE(drawn(f, "message line 99"));
    auto c = *f.card();
    Point inside{c.origin.x + DX{40}, c.origin.y + DY{c.extent.h.raw() / 2}};
    route_event(f.tree, f.snap.get(), MouseScroll{inside, DX{0}, DY{1}});
    f.frame();
    CHECK_FALSE(drawn(f, "message line 0"));
    f.click(inside.x.raw(), inside.y.raw());   // the message takes the keys once clicked
    for (int i = 0; i < 200; ++i) f.key(keys::down);
    f.frame();
    CHECK(drawn(f, "message line 99"));
    for (int i = 0; i < 200; ++i) f.key(keys::up);
    f.frame();
    CHECK(drawn(f, "message line 0"));
    CHECK(f.model.ask.get().message == numbered(100));   // read-only: the keys changed nothing
}

TEST_CASE("a very long line wraps instead of widening the card past the window") {
    Fixture f;
    std::string line;
    for (int i = 0; i < 60; ++i) line += "word ";
    show_message(f, line);
    auto c = f.card();
    REQUIRE(c);
    CHECK(c->extent.w.raw() <= 600.f);
    CHECK(drawn_starting_with(f, "word") > 1);
}

TEST_CASE("a dialog the app closed and reopened starts at the top of its message") {
    Fixture f;
    show_message(f, numbered(100));
    auto c = *f.card();
    route_event(f.tree, f.snap.get(), MouseScroll{Point{c.origin.x + DX{40}, c.origin.y + DY{80}}, DX{0}, DY{3}});
    f.frame();
    REQUIRE_FALSE(drawn(f, "message line 0"));
    auto d = f.model.ask.get();
    d.open = false;   // the app closes it
    f.model.ask.set(d);
    f.frame();
    show_message(f, numbered(100));
    CHECK(drawn(f, "message line 0"));
}

TEST_CASE("an open dialog keeps its content across frames: a scrolled message stays scrolled") {
    Fixture f;
    show_message(f, numbered(100));
    auto c = *f.card();
    route_event(f.tree, f.snap.get(), MouseScroll{Point{c.origin.x + DX{40}, c.origin.y + DY{c.extent.h.raw() / 2}}, DX{0}, DY{1}});
    f.frame();
    f.frame();   // nothing changed: the content is not rebuilt
    CHECK_FALSE(drawn(f, "message line 0"));
}

TEST_CASE("a dialog can hold a form: its fields take typing, its buttons still choose") {
    struct Rename {
        Field<Dialog> ask{{.title = "Rename", .message = "", .buttons = {"Rename", "Cancel"}}};
        Field<TextField<>> name{{.value = "notes.txt"}};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.dialog(ask, [&](WidgetTree::ViewBuilder& d) { d.widget(name).focus_on_open(); });
        }
    } model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto d = model.ask.get();
    d.open = true;
    model.ask.set(d);
    snap = tree.build_snapshot(600, 400, 2);
    route_event(tree, snap.get(), KeyPress{keys::end, 0});
    route_event(tree, snap.get(), TextInput{".bak"});
    CHECK(model.name.get().value == "notes.txt.bak");
    route_event(tree, snap.get(), KeyPress{keys::tab, 0});   // to "Rename"
    route_event(tree, snap.get(), KeyPress{keys::enter, 0});
    CHECK(model.ask.get().chosen == std::optional<std::size_t>{0});
    CHECK_FALSE(model.ask.get().open);
}
