#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>

#include <vector>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

// A stand-in menu: the first press opens a popup below it; later events are recorded.
struct Menuish { bool modal = false; bool operator==(const Menuish&) const = default; };
struct Victim { int n = 0; bool operator==(const Victim&) const = default; };
static std::vector<prism::InputEvent> menu_events, victim_events;

namespace prism::ui {
template <> struct Widget<Menuish> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static void record(DrawList& dl, const Field<Menuish>&, WidgetNode& node) {
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{30}}}, Color::rgba(1, 1, 1));
        (void)node;
    }
    static void handle_input(Field<Menuish>& f, const InputEvent& ev, WidgetNode& node) {
        auto* mb = std::get_if<MouseButton>(&ev);
        if (mb && mb->pressed && !node.popup_area) {
            node.popup_area = Rect{Point{X{0}, Y{30}}, Size{Width{100}, Height{60}}};   // below, local
            node.popup_modal = f.get().modal;
            return;
        }
        menu_events.push_back(ev);
    }
};
template <> struct Widget<Victim> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static void record(DrawList& dl, const Field<Victim>&, WidgetNode&) {
        dl.filled_rect(Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{100}}}, Color::rgba(2, 2, 2));
    }
    static void handle_input(Field<Victim>&, const InputEvent& ev, WidgetNode&) { victim_events.push_back(ev); }
};
}

namespace {
struct Screen {
    prism::Field<Menuish> menu{{}};
    prism::Field<Victim> victim{{}};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(menu, victim); }
};
using prism::app::widget_detail::route_event;

struct Fixture {
    Screen model;
    prism::WidgetTree tree{model};
    std::unique_ptr<prism::SceneSnapshot> snap = tree.build_snapshot(300, 200, 1);
    prism::WidgetId menu_id = tree.leaf_ids()[0], victim_id = tree.leaf_ids()[1];
    Fixture(bool modal = false) {
        model.menu.set({modal});
        menu_events.clear();
        victim_events.clear();
        press(10, 10);   // opens the popup: area x 0..100, y 30..90 (window coords)
        release(10, 10);
        menu_events.clear();
    }
    void press(float x, float y) { route_event(tree, snap.get(), prism::MouseButton{prism::Point{prism::X{x}, prism::Y{y}}, 1, true}); }
    void release(float x, float y) { route_event(tree, snap.get(), prism::MouseButton{prism::Point{prism::X{x}, prism::Y{y}}, 1, false}); }
    bool open() const { return tree.popup_owner() == menu_id; }
};
}

TEST_CASE("a press inside the popup goes to its owner") {
    Fixture f;
    REQUIRE(f.open());
    f.press(50, 50);   // inside the popup, over the victim
    CHECK(menu_events.size() == 1);
    CHECK(victim_events.empty());
}

TEST_CASE("a press outside closes the popup and reaches nothing underneath") {
    Fixture f;
    f.press(150, 60);   // outside the popup, on the victim
    CHECK_FALSE(f.open());
    CHECK(victim_events.empty());
    CHECK(f.tree.focused_id() == f.menu_id);   // focus did not move to the victim
}

TEST_CASE("escape closes the popup") {
    Fixture f;
    route_event(f.tree, f.snap.get(), prism::KeyPress{prism::keys::escape, 0});
    CHECK_FALSE(f.open());
}

TEST_CASE("a modal popup swallows outside presses and stays open") {
    Fixture f{true};
    f.press(150, 60);
    CHECK(f.open());
    CHECK(victim_events.empty());
    route_event(f.tree, f.snap.get(), prism::KeyPress{prism::keys::escape, 0});
    CHECK(f.open());   // a modal popup is closed by its owner, not by escape
    CHECK(menu_events.size() == 1);   // the owner received the escape key
}

TEST_CASE("keys and moves go to the popup, not to what is underneath") {
    Fixture f;
    f.tree.set_focused(f.victim_id);
    route_event(f.tree, f.snap.get(), prism::KeyPress{prism::keys::down, 0});
    route_event(f.tree, f.snap.get(), prism::MouseMove{prism::Point{prism::X{150}, prism::Y{60}}});
    CHECK(victim_events.empty());
    CHECK(f.tree.hovered_id() != f.victim_id);
    CHECK(menu_events.size() >= 1);
}
