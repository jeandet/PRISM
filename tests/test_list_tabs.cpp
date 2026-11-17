// Tabs over a list (vb.tabs(selected, items, title, page)): one tab per item, kept in step with
// the list; .closable() puts a close button on them, .reorderable() lets them be dragged along
// the bar. Both act on the list, the app's own data.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <map>
#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace list_tabs_test {
struct Files {
    List<std::string> open;
    Field<TabBar<>> current;
    std::map<std::string, Field<Label<>>> bodies;
    Files() {
        for (auto name : {"a.cpp", "b.cpp", "c.cpp", "d.cpp"}) {
            open.push_back(name);
            bodies.emplace(name, Label<>{std::string("BODY ") + name});
        }
    }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.tabs(current, open, [](const std::string& f) { return f; },
                [this](WidgetTree::ViewBuilder& p, const std::string& f) { p.widget(bodies.at(f)); })
            .closable([this](std::size_t i) { return open[i] != "a.cpp"; })
            .reorderable();
    }
    std::vector<std::string> names() const { return {open.begin(), open.end()}; }
};
} // namespace list_tabs_test

using list_tabs_test::Files;

namespace {
std::optional<Point> text_at(const SceneSnapshot& snap, std::string_view text) {
    for (auto& dl : snap.draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return t->origin;
    return std::nullopt;
}

// The middle of the close button in `title`'s tab: its lines lie before the next tab's title.
std::optional<Point> close_button(const SceneSnapshot& snap, std::string_view title) {
    for (auto& dl : snap.draw_lists) {
        std::optional<Point> text;
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == title) text = t->origin;
        if (!text) continue;
        float next = 1e9f;
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->origin.x.raw() > text->x.raw()) next = std::min(next, t->origin.x.raw());
        for (auto& c : dl->commands)
            if (auto* l = std::get_if<Line>(&c); l && l->from.x.raw() > text->x.raw() && l->from.x.raw() < next)
                return Point{X{(l->from.x.raw() + l->to.x.raw()) / 2.f}, Y{(l->from.y.raw() + l->to.y.raw()) / 2.f}};
    }
    return std::nullopt;
}

void click(WidgetTree& tree, const SceneSnapshot& snap, Point p, uint8_t button = buttons::left) {
    route_event(tree, &snap, MouseButton{p, button, true});
    route_event(tree, &snap, MouseButton{p, button, false});
}
} // namespace

TEST_CASE("a tab per item, titled by the item, and the selected one's page") {
    Files model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    for (auto name : {"a.cpp", "b.cpp", "c.cpp", "d.cpp"}) CHECK(text_at(*snap, name));
    CHECK(text_at(*snap, "BODY a.cpp"));
    CHECK_FALSE(text_at(*snap, "BODY b.cpp"));
}

TEST_CASE("the close button removes the item from the list; a tab it does not allow has none") {
    Files model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    CHECK_FALSE(close_button(*snap, "a.cpp"));
    auto x = close_button(*snap, "b.cpp");
    REQUIRE(x);
    click(tree, *snap, *x);
    CHECK(model.names() == std::vector<std::string>{"a.cpp", "c.cpp", "d.cpp"});
    snap = tree.build_snapshot(600, 400, 2);
    CHECK_FALSE(text_at(*snap, "b.cpp"));
    CHECK(text_at(*snap, "d.cpp"));
}

TEST_CASE("closing keeps the same item shown; closing the shown last one shows the one before") {
    Files model;
    model.current.set({.selected = 2});   // c.cpp
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    click(tree, *snap, *close_button(*snap, "b.cpp"));
    CHECK(model.names()[model.current.get().selected] == "c.cpp");

    snap = tree.build_snapshot(600, 400, 2);
    route_event(tree, snap.get(), MouseButton{*text_at(*snap, "d.cpp"), buttons::left, true});
    route_event(tree, snap.get(), MouseButton{*text_at(*snap, "d.cpp"), buttons::left, false});
    snap = tree.build_snapshot(600, 400, 3);
    click(tree, *snap, *close_button(*snap, "d.cpp"));
    CHECK(model.names()[model.current.get().selected] == "c.cpp");
    snap = tree.build_snapshot(600, 400, 4);
    CHECK(text_at(*snap, "BODY c.cpp"));
}

TEST_CASE("a middle click closes a tab that allows it") {
    Files model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    click(tree, *snap, *text_at(*snap, "a.cpp"), buttons::middle);
    click(tree, *snap, *text_at(*snap, "c.cpp"), buttons::middle);
    CHECK(model.names() == std::vector<std::string>{"a.cpp", "b.cpp", "d.cpp"});
}

TEST_CASE("dragging a tab along the bar moves its item in the list, and shows it") {
    Files model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto c = text_at(*snap, "c.cpp"), a = text_at(*snap, "a.cpp");
    REQUIRE(c);
    REQUIRE(a);
    Point from{c->x + DX{5}, c->y + DY{5}}, to{a->x + DX{2}, a->y + DY{5}};   // before a.cpp
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x - DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{to});
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(model.names() == std::vector<std::string>{"c.cpp", "a.cpp", "b.cpp", "d.cpp"});
    CHECK(model.current.get().selected == 0);
    snap = tree.build_snapshot(600, 400, 2);
    CHECK(text_at(*snap, "BODY c.cpp"));
    CHECK(text_at(*snap, "c.cpp")->x.raw() < text_at(*snap, "a.cpp")->x.raw());
}

TEST_CASE("an item added to the list gets its tab") {
    Files model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    model.bodies.emplace("e.cpp", Label<>{"BODY e.cpp"});
    model.open.push_back("e.cpp");
    snap = tree.build_snapshot(600, 400, 2);
    CHECK(text_at(*snap, "e.cpp"));
}
