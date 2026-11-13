// Dragging inside the app (doc/design/drag-and-docking.md, part 1): a source gives a typed value,
// a target accepts it by type.
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

namespace drag_test {
struct Track {
    int id = 0;
};

struct Board {
    Field<Button> track{{"Song"}};             // a source: drags a Track
    Field<Label<>> playlist{{"Playlist"}};     // a target for Tracks
    Field<Label<>> bin{{"Bin"}};               // a target for strings only
    Field<TextField<>> note{{}};               // a text field: takes a dragged string as typing
    Field<Label<>> word{{"word"}};             // a source: drags a std::string
    Field<bool> playlist_on{true};
    std::vector<int> dropped;
    std::optional<DropPlace> place;
    int bin_drops = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(track).drag_source([] { return Track{7}; });
            vb.widget(playlist).enabled(playlist_on).drop_target([this](const Track& t, DropPlace at) {
                dropped.push_back(t.id);
                place = at;
            });
            vb.widget(bin).drop_target([this](const std::string&, DropPlace) { ++bin_drops; });
            vb.widget(note);
            vb.widget(word).drag_source([] { return std::string{"hello"}; });
        });
    }
};
}  // namespace drag_test

using namespace drag_test;

namespace {
struct Fixture {
    Board model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    std::vector<WidgetId> ids = tree.leaf_ids();   // track, playlist, bin, note, word
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    Point centre(WidgetId id) {
        auto r = find_widget_rect(*snap, id);
        REQUIRE(r);
        return Point{r->origin.x + DX{r->extent.w.raw() / 2}, r->origin.y + DY{r->extent.h.raw() / 2}};
    }
    void press(Point p) { route_event(tree, snap.get(), MouseButton{p, buttons::left, true}); }
    void move(Point p) { route_event(tree, snap.get(), MouseMove{p}); }
    void release(Point p) { route_event(tree, snap.get(), MouseButton{p, buttons::left, false}); }
    void drag(WidgetId from, WidgetId to) {
        auto a = centre(from), b = centre(to);
        press(a);
        move(Point{a.x + DX{10}, a.y});
        move(b);
        release(b);
    }
};
}

TEST_CASE("dragging a source onto a target that takes its type drops the value there") {
    Fixture f;
    f.drag(f.ids[0], f.ids[1]);
    CHECK(f.model.dropped == std::vector<int>{7});
    REQUIRE(f.model.place);
    auto r = find_widget_rect(*f.snap, f.ids[1]);
    CHECK(f.model.place->at.x.raw() == doctest::Approx(r->extent.w.raw() / 2));   // in the target's coordinates
    CHECK(f.model.place->size.h.raw() == doctest::Approx(r->extent.h.raw()));
    CHECK(f.model.track.get().click_count == 0);   // a drag is not a click
}

TEST_CASE("a press and release without moving far is a click, not a drag") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{2}, a.y});
    f.release(Point{a.x + DX{2}, a.y});
    CHECK(f.model.track.get().click_count == 1);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("a target of another type refuses the drop and shows the no-entry cursor") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[2]));   // the bin takes strings, not Tracks
    CHECK(f.tree.desired_cursor() == CursorShape::NotAllowed);
    f.move(f.centre(f.ids[1]));
    CHECK(f.tree.desired_cursor() == CursorShape::Default);
    f.move(f.centre(f.ids[2]));
    f.release(f.centre(f.ids[2]));
    CHECK(f.model.bin_drops == 0);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("Escape cancels a drag") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[1]));
    route_event(f.tree, f.snap.get(), KeyPress{keys::escape, 0});
    f.release(f.centre(f.ids[1]));
    CHECK(f.model.dropped.empty());
    CHECK(f.tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("a disabled target takes nothing") {
    Fixture f;
    f.model.playlist_on.set(false);
    f.frame();
    f.drag(f.ids[0], f.ids[1]);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("a dragged string dropped on a text field is typed into it") {
    Fixture f;
    f.drag(f.ids[4], f.ids[3]);
    CHECK(f.model.note.get().value == "hello");
}

TEST_CASE("while dragging, the source follows the pointer, faded, and the target is outlined") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[1]));
    f.frame();
    bool ghost = false;
    for (auto& c : f.snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "Song") ghost = t->color.a < 255;
    CHECK(ghost);
    auto target = find_widget_rect(*f.snap, f.ids[1]);
    bool outlined = false;
    for (auto& c : f.snap->overlay.commands)
        if (auto* o = std::get_if<RectOutline>(&c); o && o->rect.origin == target->origin) outlined = true;
    CHECK(outlined);
    f.release(f.centre(f.ids[1]));
    f.frame();
    for (auto& c : f.snap->overlay.commands)
        CHECK_FALSE((std::holds_alternative<TextCmd>(c) && std::get<TextCmd>(c).text == "Song"));
}

TEST_CASE("a click lands on release: sliding off the button before letting go cancels it") {
    Fixture f;
    auto ok = f.centre(f.ids[0]);
    f.press(ok);
    f.release(ok);
    CHECK(f.model.track.get().click_count == 1);
    f.press(ok);
    CHECK(f.model.track.get().click_count == 1);   // not yet: on release
    auto off = Point{X{ok.x.raw()}, Y{299}};       // well below the button (no target there)
    f.release(off);
    CHECK(f.model.track.get().click_count == 1);
}
