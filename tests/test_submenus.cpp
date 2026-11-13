// Submenus: a menu item holding items of its own opens them beside it (ui::MenuItem::items).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
using Path = std::vector<std::size_t>;

struct App {
    Field<Menu> file{{.title = "File", .items = {
        {.text = "New"},
        {.text = "Recent", .items = {
            {.text = "notes.txt"},
            {.text = "todo.txt", .shortcut = {int32_t{'t'}, mods::ctrl}},
            {.text = "More", .items = {{.text = "old.txt"}}},
        }},
        {.text = "Quit"},
    }}};
    Field<Menu> edit{{.title = "Edit", .items = {{.text = "Undo"}}}};
    Field<Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.hstack(file, edit);
            vb.widget(ok);
        });
    }
};

// The File popup's rows start at y = 30 and are 28 high: New 30..58, Recent 58..86, Quit 86..114.
// Recent's submenu opens to the right of the popup, its first row level with Recent.
struct Fixture {
    App model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(600, 400, 1);
    uint64_t v = 1;
    std::vector<WidgetId> ids = tree.leaf_ids();   // file, edit, ok
    std::vector<Path> chosen;
    Connection link = on_choice(model.file, [this](const Path& p) { chosen.push_back(p); });

    void frame() { snap = tree.build_snapshot(600, 400, ++v); }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); frame(); }
    void move(float x, float y) { route_event(tree, snap.get(), MouseMove{Point{X{x}, Y{y}}}); frame(); }
    void click(float x, float y) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false});
        frame();
    }
    bool open() const { return tree.popup_owner() == ids[0]; }
    bool shows(std::string_view text) const {
        for (auto& c : snap->overlay.commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    // The right edge of the File popup: where Recent's submenu starts.
    float popup_right() const { return tree.popup_node()->popup_area->extent.w.raw(); }
};
}

TEST_CASE("hovering an item with a submenu opens it; clicking in it chooses by path") {
    Fixture f;
    f.click(10, 10);
    REQUIRE(f.open());
    CHECK_FALSE(f.shows("notes.txt"));
    f.move(10, 72);   // Recent
    CHECK(f.shows("notes.txt"));
    float sub_x = f.popup_right() + 10;
    f.move(sub_x, 100);   // into the submenu: todo.txt, its second row
    CHECK(f.open());
    f.click(sub_x, 100);
    CHECK_FALSE(f.open());
    CHECK(f.chosen == std::vector<Path>{{1, 1}});
    CHECK(f.model.file.get().chosen_path == Path{1, 1});
}

TEST_CASE("clicking an item with a submenu opens it rather than choosing it") {
    Fixture f;
    f.click(10, 10);
    f.click(10, 72);
    CHECK(f.open());
    CHECK(f.shows("notes.txt"));
    CHECK(f.chosen.empty());
}

TEST_CASE("moving to another item closes the submenu") {
    Fixture f;
    f.click(10, 10);
    f.move(10, 72);
    REQUIRE(f.shows("notes.txt"));
    f.move(10, 100);   // Quit
    CHECK_FALSE(f.shows("notes.txt"));
    CHECK(f.open());
}

TEST_CASE("Right opens a submenu, Left and Escape close only it, Enter chooses in it") {
    Fixture f;
    f.click(10, 10);
    f.key(keys::down);    // Recent
    f.key(keys::right);   // opens it, not the Edit menu
    CHECK(f.open());
    CHECK(f.shows("notes.txt"));
    f.key(keys::left);    // closes it, not a move to the Edit menu
    CHECK(f.open());
    CHECK_FALSE(f.shows("notes.txt"));
    f.key(keys::right);
    f.key(keys::escape);  // the submenu only
    CHECK(f.open());
    CHECK_FALSE(f.shows("notes.txt"));
    f.key(keys::right);
    f.key(keys::down);    // todo.txt
    f.key(keys::down);    // More
    f.key(keys::right);   // a submenu in the submenu
    CHECK(f.shows("old.txt"));
    f.key(keys::enter);
    CHECK(f.chosen == std::vector<Path>{{1, 2, 0}});
    CHECK_FALSE(f.open());
}

TEST_CASE("Left and Right still switch menus in the bar where no submenu is involved") {
    Fixture f;
    f.click(10, 10);
    f.key(keys::right);   // New has no submenu: on to Edit
    CHECK(f.tree.popup_owner() == f.ids[1]);
}

TEST_CASE("a shortcut inside a submenu chooses its item from anywhere") {
    Fixture f;
    f.tree.set_focused(f.ids[2]);
    f.key(int32_t{'t'}, mods::ctrl);
    CHECK(f.chosen == std::vector<Path>{{1, 1}});
    CHECK_FALSE(f.open());
}

TEST_CASE("an index callback hears top-level items only") {
    Fixture f;
    std::vector<std::size_t> top;
    auto c = on_choice(f.model.file, [&](std::size_t i) { top.push_back(i); });
    f.click(10, 10);
    f.click(10, 100);   // Quit
    f.key(int32_t{'t'}, mods::ctrl);   // Recent > todo.txt
    CHECK(top == std::vector<std::size_t>{2});
    CHECK(f.chosen.size() == 2);
}

TEST_CASE("an item with a submenu shows an arrow") {
    Fixture f;
    f.click(10, 10);
    CHECK(f.shows("\xe2\x80\xba"));   // ›
}

TEST_CASE("heading for the open submenu across another item keeps it open") {
    Fixture f;
    f.click(10, 10);
    f.move(10, 72);   // Recent: its submenu spans y 58..142, right of the popup
    REQUIRE(f.shows("notes.txt"));
    float edge = f.popup_right();
    f.move(edge - 5, 95);   // over Quit, but on the way to the submenu
    CHECK(f.shows("notes.txt"));
    f.click(edge + 10, 100);   // todo.txt
    CHECK(f.chosen == std::vector<Path>{{1, 1}});
}
