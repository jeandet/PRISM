// A fixed choice among strings known at run time (ui::Choice): a dropdown that, unlike a
// ComboBox, takes nothing else.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <string_view>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Form {
    Field<Choice> port{{.options = {"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0"}, .placeholder = "Pick a port"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(port); }
};

struct Fixture {
    Form m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(300, 200, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(300, 200, ++v); }
    void click(float x, float y) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        for (auto& c : snap->overlay.commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
};
}

TEST_CASE("nothing chosen shows the placeholder; a click on an option chooses it") {
    Fixture f;
    CHECK(f.shows("Pick a port"));
    f.click(20, 15);                 // opens the list below: rows of 28 from y = 30
    CHECK(f.shows("/dev/ttyACM0"));
    f.click(20, 30 + 28 * 2 + 14);   // the third option
    CHECK(f.m.port.get().selected == std::optional<std::size_t>{2});
    CHECK(f.shows("/dev/ttyACM0"));
    CHECK_FALSE(f.shows("Pick a port"));
}

TEST_CASE("arrow keys step through the options") {
    Fixture f;
    f.tree.set_focused(f.tree.leaf_ids()[0]);
    f.key(keys::down);
    CHECK(f.m.port.get().selected == std::optional<std::size_t>{1});
    f.key(keys::up);
    CHECK(f.m.port.get().selected == std::optional<std::size_t>{0});
}

TEST_CASE("options the app changes at run time; a choice past their end is no choice") {
    Fixture f;
    f.m.port.set({.options = {"a", "b", "c"}, .selected = 2});
    f.frame();
    f.m.port.set({.options = {"a"}, .selected = 2, .placeholder = "none"});   // the port went away
    f.frame();
    CHECK(f.shows("none"));
    f.m.port.set({.options = {}, .placeholder = "No ports"});
    f.frame();
    f.tree.set_focused(f.tree.leaf_ids()[0]);
    f.key(keys::down);   // nothing to step to: no crash, no choice
    f.click(20, 15);
    CHECK_FALSE(f.m.port.get().selected.has_value());
    CHECK(f.shows("No ports"));
}

TEST_CASE("assistive tech hears the chosen option") {
    Field<Choice> c{{.options = {"x", "y"}, .selected = 1}};
    AccessInfo a;
    Widget<Choice>::describe(c, a);
    CHECK(Widget<Choice>::access_role == Role::ComboBox);
    CHECK(a.value == "y");
}
