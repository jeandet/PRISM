#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

using prism::app::widget_detail::route_event;

namespace {
struct Bar {
    prism::Field<prism::Menu> file{{.title = "File", .items = {"Open", "Save", "Quit"}}};
    prism::Field<prism::Label<>> below{{"content"}};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.vstack(file, below); }
};

struct Fixture {
    Bar model;
    prism::WidgetTree tree{model};
    std::unique_ptr<prism::SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    prism::WidgetId menu_id = tree.leaf_ids()[0];
    void click(float x, float y) {
        route_event(tree, snap.get(), prism::MouseButton{prism::Point{prism::X{x}, prism::Y{y}}, 1, true});
        route_event(tree, snap.get(), prism::MouseButton{prism::Point{prism::X{x}, prism::Y{y}}, 1, false});
    }
    void key(int32_t k) { route_event(tree, snap.get(), prism::KeyPress{k, 0}); }
    bool open() const { return tree.popup_owner() == menu_id; }
};
}

TEST_CASE("clicking the menu opens it; clicking an item chooses it and closes the menu") {
    Fixture f;
    f.click(10, 10);
    REQUIRE(f.open());
    // items are 28 px rows below the 30 px title: "Save" is the second row
    f.click(10, 30 + 28 + 14);
    CHECK_FALSE(f.open());
    CHECK(f.model.file.get().chosen == std::optional<size_t>{1});
    CHECK(f.model.file.get().activations == 1);
}

TEST_CASE("the keyboard opens the menu, moves the highlight and chooses") {
    Fixture f;
    f.tree.set_focused(f.menu_id);
    f.key(prism::keys::enter);
    REQUIRE(f.open());
    f.key(prism::keys::down);
    f.key(prism::keys::down);
    f.key(prism::keys::enter);
    CHECK(f.model.file.get().chosen == std::optional<size_t>{2});
    CHECK_FALSE(f.open());
}

TEST_CASE("escape or a press outside closes the menu without choosing") {
    Fixture f;
    f.click(10, 10);
    f.key(prism::keys::escape);
    CHECK_FALSE(f.open());
    f.click(10, 10);
    f.click(350, 250);
    CHECK_FALSE(f.open());
    CHECK_FALSE(f.model.file.get().chosen.has_value());
    CHECK(f.model.file.get().activations == 0);
}

TEST_CASE("choosing the same item twice notifies twice") {
    Fixture f;
    int notified = 0;
    auto c = f.model.file.on_change().connect([&](const prism::Menu&) { ++notified; });
    f.click(10, 10);
    f.click(10, 30 + 14);
    f.click(10, 10);
    f.click(10, 30 + 14);
    CHECK(notified == 2);
}
