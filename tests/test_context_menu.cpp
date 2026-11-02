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
    Field<Label<>> title{{"title"}};
    Field<Label<>> body{{"body"}};
    Field<ContextMenu> menu{{.items = {"Copy", "Paste", "Select all"}}};
    Field<Menu> file{{.title = "File", .items = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"}}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(file);
            vb.widget(title);
            vb.widget(body);
            vb.context_menu(menu);
        });
    }
};

struct Fixture {
    Doc model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    void press(float x, float y, uint8_t button) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, button, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, button, false});
    }
    void refresh() { snap = tree.build_snapshot(400, 300, 2); }
    std::optional<Rect> area() const { return widget_detail::popup_area(tree, *snap); }
};
}

TEST_CASE("snapshot knows the window size") {
    Fixture f;
    CHECK(f.snap->viewport == Size{Width{400}, Height{300}});
}

TEST_CASE("a right click opens the context menu at the pointer; a left click on an item chooses it") {
    Fixture f;
    f.press(50, 60, buttons::right);
    REQUIRE(f.tree.popup_owner() != 0);
    auto a = f.area();
    REQUIRE(a);
    CHECK(a->origin.x.raw() == doctest::Approx(50.f));
    CHECK(a->origin.y.raw() == doctest::Approx(60.f));

    f.refresh();
    f.press(60, 60 + 28 + 10, buttons::left);   // second row
    CHECK(f.tree.popup_owner() == 0);
    CHECK(f.model.menu.get().chosen == std::optional<size_t>{1});
    CHECK(f.model.menu.get().activations == 1);
}

TEST_CASE("a context menu opened near the bottom-right corner stays inside the window") {
    Fixture f;
    f.press(390, 290, buttons::right);
    auto a = f.area();
    REQUIRE(a);
    CHECK(a->origin.x.raw() + a->extent.w.raw() <= 400.f);
    CHECK(a->origin.y.raw() + a->extent.h.raw() <= 300.f);
    CHECK(a->origin.x.raw() >= 0.f);
    CHECK(a->origin.y.raw() >= 0.f);

    // and the clamped popup is still usable: its first row chooses item 0
    f.refresh();
    f.press(a->origin.x.raw() + 5, a->origin.y.raw() + 5, buttons::left);
    CHECK(f.model.menu.get().chosen == std::optional<size_t>{0});
}

TEST_CASE("a menu too tall for the window below its title is moved up to fit") {
    Fixture f;
    f.press(10, 10, buttons::left);   // File: 10 items * 28 = 280 px below a 30 px title
    auto a = f.area();
    REQUIRE(a);
    CHECK(a->origin.y.raw() + a->extent.h.raw() <= 300.f);
    f.refresh();
    f.press(15, a->origin.y.raw() + 28 * 9 + 5, buttons::left);   // last row
    CHECK(f.model.file.get().chosen == std::optional<size_t>{9});
}

TEST_CASE("a right click outside any context menu's container does nothing") {
    struct Plain {
        Field<Label<>> l{{"x"}};
        void view(WidgetTree::ViewBuilder& vb) { vb.widget(l); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::right, true});
    CHECK(tree.popup_owner() == 0);
}

TEST_CASE("a right click on empty space inside the menu's container opens it too") {
    Fixture f;
    f.press(10, 290, buttons::right);   // below the last label, still inside the vstack
    CHECK(f.tree.popup_owner() != 0);
}

TEST_CASE("opening a menu with a click leaves no widget captured") {
    Fixture f;
    f.press(10, 10, buttons::left);   // opens File (press + release)
    REQUIRE(f.tree.popup_owner() != 0);
    CHECK(f.tree.captured_id() == 0);
}

TEST_CASE("a right click inside an open context menu doesn't move it") {
    Fixture f;
    f.press(50, 60, buttons::right);
    f.refresh();
    f.press(60, 70, buttons::right);
    auto a = f.area();
    REQUIRE(a);
    CHECK(a->origin.x.raw() == doctest::Approx(50.f));
}

TEST_CASE("popup anchors are not listed as leaves") {
    Fixture f;
    CHECK(f.tree.leaf_ids().size() == 3);   // File, title, body
}

TEST_CASE("empty space in a nested container opens that container's menu, not a sibling's") {
    struct TwoPanes {
        Field<Label<>> left{{"left"}};
        Field<Label<>> right{{"right"}};
        Field<ContextMenu> left_menu{{.items = {"L"}}};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.hstack([&] {
                vb.vstack([&] {
                    vb.widget(left);
                    vb.context_menu(left_menu);
                });
                vb.vstack([&] { vb.widget(right); });
            });
        }
    } m;
    WidgetTree tree{m};
    (void)tree.build_snapshot(400, 300, 1);
    auto snap = tree.build_snapshot(400, 300, 2);
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{250}}, buttons::right, true});
    CHECK(tree.popup_owner() != 0);   // empty area of the left pane
    tree.close_popup();
    route_event(tree, snap.get(), MouseButton{Point{X{390}, Y{250}}, buttons::right, true});
    CHECK(tree.popup_owner() == 0);   // the right pane has no context menu
}

TEST_CASE("a menu away from the window corner is clamped in window coordinates") {
    struct Bar {
        Field<Label<>> pad{{"a long label that pushes the menu to the right edge......."}};
        Field<Menu> help{{.title = "Help", .items = {"About this application and its many options"}}};
        void view(WidgetTree::ViewBuilder& vb) { vb.hstack(pad, help); }
    } m;
    WidgetTree tree{m};
    (void)tree.build_snapshot(400, 300, 1);
    auto snap = tree.build_snapshot(400, 300, 2);
    auto title = find_widget_rect(*snap, tree.leaf_ids()[1]);
    REQUIRE(title);
    REQUIRE(title->origin.x.raw() > 100.f);
    route_event(tree, snap.get(), MouseButton{Point{title->origin.x + DX{5}, Y{10}}, buttons::left, true});
    auto a = widget_detail::popup_area(tree, *snap);
    REQUIRE(a);
    CHECK(a->origin.x.raw() + a->extent.w.raw() <= 400.f + 0.01f);
}

// --- flipping at the window edges ---------------------------------------------

namespace {
Rect rect(float x, float y, float w, float h) { return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
const Size window{Width{400}, Height{300}};
const Size popup{Width{100}, Height{80}};
}

TEST_CASE("place_popup: below and left-aligned to its anchor when that fits") {
    auto r = widget_detail::place_popup(popup, rect(20, 10, 60, 30), window);
    CHECK(r.origin.x.raw() == doctest::Approx(20.f));
    CHECK(r.origin.y.raw() == doctest::Approx(40.f));
}

TEST_CASE("place_popup: above its anchor when it does not fit below") {
    auto r = widget_detail::place_popup(popup, rect(20, 250, 60, 30), window);
    CHECK(r.origin.y.raw() + r.extent.h.raw() == doctest::Approx(250.f));   // ends at the anchor's top
}

TEST_CASE("place_popup: right-aligned to its anchor when it does not fit to the right") {
    auto r = widget_detail::place_popup(popup, rect(350, 10, 40, 30), window);
    CHECK(r.origin.x.raw() + r.extent.w.raw() == doctest::Approx(390.f));   // ends at the anchor's right
}

TEST_CASE("place_popup: fitting on neither side, it takes the roomier side and stays inside") {
    Size tall{Width{100}, Height{200}};
    auto r = widget_detail::place_popup(tall, rect(20, 130, 60, 30), window);   // 140 below, 130 above
    CHECK(r.origin.y.raw() >= 0.f);
    CHECK(r.origin.y.raw() + r.extent.h.raw() <= 300.f);
    CHECK(r.origin.y.raw() == doctest::Approx(100.f));   // below, pushed up just enough
}

TEST_CASE("a context menu near the bottom-right corner opens up and to the left of the click") {
    Fixture f;
    f.press(390, 290, buttons::right);
    auto a = f.area();
    REQUIRE(a);
    CHECK(a->origin.x.raw() + a->extent.w.raw() == doctest::Approx(390.f));
    CHECK(a->origin.y.raw() + a->extent.h.raw() == doctest::Approx(290.f));
}

namespace {
struct RightMenuBar {
    Field<Label<>> pad{{"a long label that pushes the menu to the right edge......."}};
    Field<Menu> help{{.title = "Help", .items = {"About this application and its many options"}}};
    void view(WidgetTree::ViewBuilder& vb) { vb.hstack(pad, help); }
};
}

TEST_CASE("a menu at the right edge lines up with the right of its title") {
    RightMenuBar m;
    WidgetTree tree{m};
    (void)tree.build_snapshot(400, 300, 1);
    auto snap = tree.build_snapshot(400, 300, 2);
    auto title = find_widget_rect(*snap, tree.leaf_ids()[1]);
    REQUIRE(title);
    route_event(tree, snap.get(), MouseButton{Point{title->origin.x + DX{5}, Y{10}}, buttons::left, true});
    auto a = widget_detail::popup_area(tree, *snap);
    REQUIRE(a);
    REQUIRE(title->origin.x.raw() + a->extent.w.raw() > 400.f);   // it would overflow to the right
    float title_right = title->origin.x.raw() + title->extent.w.raw();
    CHECK(a->origin.x.raw() + a->extent.w.raw() == doctest::Approx(std::min(title_right, 400.f)));
}

TEST_CASE("closing a popup any way forgets what it opened from") {
    Fixture f;
    f.press(50, 60, buttons::right);
    REQUIRE(f.tree.popup_node());
    REQUIRE(f.tree.popup_node()->popup_opened_from);
    const WidgetNode* owner = f.tree.popup_node();
    f.tree.close_popup();
    CHECK_FALSE(owner->popup_opened_from);
}
