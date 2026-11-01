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
