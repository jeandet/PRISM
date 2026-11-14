// A Field<ui::TimeOfDay> shows "HH:MM": a click picks the hour or the minutes, Up/Down or the
// wheel step that part, digits type it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <string>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

TEST_CASE("text: two digits each; only real times are read") {
    CHECK(time_detail::to_text({9, 5}) == "09:05");
    CHECK(time_detail::parse("14:30") == TimeOfDay{14, 30});
    CHECK_FALSE(time_detail::parse("24:00"));
    CHECK_FALSE(time_detail::parse("12:60"));
    CHECK_FALSE(time_detail::parse("9:05"));
}

TEST_CASE("a step wraps within its part") {
    CHECK(time_detail::step({14, 59}, TimeEditState::Part::minute, 1) == TimeOfDay{14, 0});
    CHECK(time_detail::step({0, 10}, TimeEditState::Part::hour, -1) == TimeOfDay{23, 10});
}

namespace {
struct Alarm {
    Field<TimeOfDay> wake{{14, 30}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(wake); }
};

struct Fixture {
    Alarm m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    void click(Point at) {
        route_event(tree, snap.get(), MouseButton{at, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{at, buttons::left, false});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    TimeOfDay now() const { return m.wake.get(); }
};
}

TEST_CASE("it shows HH:MM") {
    Fixture f;
    CHECK(f.shows("14:30"));
}

TEST_CASE("a click on the minutes picks them; Up/Down step them") {
    Fixture f;
    f.click(Point{X{8 + 30}, Y{15}});   // right of the colon
    f.key(keys::up);
    CHECK(f.now() == TimeOfDay{14, 31});
    f.key(keys::down);
    f.key(keys::down);
    CHECK(f.now() == TimeOfDay{14, 29});
}

TEST_CASE("Tab lands on the hour; Right moves to the minutes") {
    Fixture f;
    f.key(keys::tab);
    f.key(keys::up);
    CHECK(f.now() == TimeOfDay{15, 30});
    f.key(keys::right);
    f.key(keys::up);
    CHECK(f.now() == TimeOfDay{15, 31});
    f.key(keys::left);
    f.key(keys::down);
    CHECK(f.now() == TimeOfDay{14, 31});
}

TEST_CASE("digits type the hour, then the minutes") {
    Fixture f;
    f.key(keys::tab);
    f.type("0");
    f.type("8");
    f.type("4");
    f.type("5");
    CHECK(f.now() == TimeOfDay{8, 45});
    f.key(keys::left);
    f.type("9");        // no hour starts with 9 and has a second digit: done at once
    f.type("7");        // already on the minutes: 7 cannot start one, so 07
    CHECK(f.now() == TimeOfDay{9, 7});
}

TEST_CASE("the wheel steps the picked part") {
    Fixture f;
    f.click(Point{X{12}, Y{15}});      // the hour
    route_event(f.tree, f.snap.get(), MouseScroll{Point{X{12}, Y{15}}, DX{0}, DY{1}});   // up, away from the user
    f.frame();
    CHECK(f.now() == TimeOfDay{15, 30});
}
