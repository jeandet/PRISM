// Buttons that stay pressed: Button::checked (a toggle, as on a toolbar).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Toolbar {
    Field<Button> bold{{.text = "Bold", .primary = false, .shortcut = {int32_t{'b'}, mods::ctrl}, .checked = false}};
    Field<Button> save{{"Save"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.hstack(bold, save); }
};

struct Fixture {
    Toolbar m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 100, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 100, ++v); }
    void click(float x, float y) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false});
        frame();
    }
    // The fill of the first button's face.
    Color face() const {
        for (auto& c : snap->draw_lists.front()->commands)
            if (auto* r = std::get_if<FilledRect>(&c)) return r->color;
        return {};
    }
};
}

TEST_CASE("a toggle button flips on each click, and still counts the clicks") {
    Fixture f;
    f.click(10, 10);
    CHECK(f.m.bold.get().checked == std::optional<bool>{true});
    CHECK(f.m.bold.get().click_count == 1);
    f.click(10, 10);
    CHECK(f.m.bold.get().checked == std::optional<bool>{false});
    CHECK(f.m.bold.get().click_count == 2);
}

TEST_CASE("its shortcut flips it too; a plain button stays a plain button") {
    Fixture f;
    route_event(f.tree, f.snap.get(), KeyPress{int32_t{'b'}, mods::ctrl});
    CHECK(f.m.bold.get().checked == std::optional<bool>{true});
    CHECK_FALSE(f.m.save.get().checked.has_value());
}

TEST_CASE("a pressed toggle looks pressed, and says so to assistive tech") {
    Fixture f;
    Color off = f.face();
    f.click(10, 10);
    f.tree.update_hover(std::nullopt);   // not the hover colour: the pressed one
    f.frame();
    CHECK(f.face() != off);
    CHECK(f.face() == f.tree.theme().surface_active);
    AccessInfo a;
    Widget<Button>::describe(f.m.bold, a);
    CHECK(a.checked == std::optional<bool>{true});
}
