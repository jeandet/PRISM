// An editable combo box: a text field with suggestions filtered as you type.
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
struct Form {
    Field<ComboBox> fruit{{.value = "", .placeholder = "Fruit", .options = {"apple", "apricot", "banana"}}};
    Field<TextField<>> other{{}};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(fruit, other); }
};

struct Fixture {
    Form model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    WidgetId combo() const { return tree.leaf_ids()[0]; }
    Point centre(WidgetId id) const {
        auto r = *find_widget_rect(*snap, id);
        return Point{r.origin.x + DX{20}, r.origin.y + DY{r.extent.h.raw() / 2}};
    }
    void click(Point p) {
        route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
        frame();
    }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); frame(); }
    void type(std::string s) {
        for (char c : s) route_event(tree, snap.get(), TextInput{std::string(1, c)});
        frame();
    }
    // The suggestions shown, top to bottom (drawn in the overlay).
    std::vector<std::string> shown() const {
        std::vector<std::string> out;
        for (auto& c : snap->overlay.commands)
            if (auto* t = std::get_if<TextCmd>(&c)) out.push_back(t->text);
        return out;
    }
};
}

TEST_CASE("typing filters the suggestions") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("ap");
    CHECK(f.model.fruit.get().value == "ap");
    CHECK(f.shown() == std::vector<std::string>{"apple", "apricot"});
}

TEST_CASE("Down then Enter takes the highlighted suggestion and closes the list") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("ap");
    f.key(keys::down);   // apricot (apple is highlighted first)
    f.key(keys::enter);
    CHECK(f.model.fruit.get().value == "apricot");
    CHECK(f.shown().empty());
}

TEST_CASE("clicking a suggestion takes it") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("an");
    REQUIRE(f.shown() == std::vector<std::string>{"banana"});
    Rect list{};
    for (auto& [id, r] : f.snap->overlay_geometry) if (id == f.combo()) list = r;
    f.click(Point{list.origin.x + DX{10}, list.origin.y + DY{8}});
    CHECK(f.model.fruit.get().value == "banana");
}

TEST_CASE("Escape closes the list and keeps what was typed") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("ap");
    f.key(keys::escape);
    CHECK(f.shown().empty());
    CHECK(f.model.fruit.get().value == "ap");
}

TEST_CASE("leaving the combo box keeps its undo history") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("ap");
    REQUIRE_FALSE(f.shown().empty());            // the list is open
    f.click(Point{X{395}, Y{295}});              // elsewhere: an empty spot (the list covers the field below)
    f.click(f.centre(f.combo()));
    f.key(keys::z, mods::ctrl);
    CHECK(f.model.fruit.get().value != "ap");   // the typing could be undone
}

TEST_CASE("after a pick, clicking the box or pressing Down shows the suggestions again") {
    Fixture f;
    f.click(f.centre(f.combo()));
    f.type("ap");
    f.key(keys::enter);
    REQUIRE(f.shown().empty());
    f.click(f.centre(f.combo()));
    CHECK_FALSE(f.shown().empty());
    f.key(keys::escape);
    REQUIRE(f.shown().empty());
    f.key(keys::down);
    CHECK_FALSE(f.shown().empty());
}
