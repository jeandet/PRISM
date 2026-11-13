// Docking (doc/design/drag-and-docking.md, part 2): a layout value the app owns, built into
// splits and tab groups.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/dock.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

TEST_CASE("a layout is fitted to the panels the app has") {
    DockLayout saved{dock_split(false, {dock_group({"files", "gone"}), dock_group({"editor"})})};
    auto fit = fitted(saved, {"files", "editor", "search"});
    // "gone" no longer exists; "search" is new and joins the first group.
    CHECK(fit.root == dock_split(false, {dock_group({"files", "search"}), dock_group({"editor"})}));
    // A group left empty disappears, and a split left with one part becomes that part.
    auto lone = fitted(saved, {"editor"});
    CHECK(lone.root == dock_group({"editor"}));
    // Nothing saved: every panel in one group.
    CHECK(fitted(DockLayout{}, {"a", "b"}).root == dock_group({"a", "b"}));
}

TEST_CASE("sizes and the shown tab are not structure") {
    auto a = dock_split(false, {dock_group({"x", "y"}), dock_group({"z"})});
    auto b = a;
    b.sizes = {100.f, 300.f};
    b.parts[0].active = 1;
    CHECK(same_structure(a, b));
    b.parts[0].panels = {"y", "x"};
    CHECK_FALSE(same_structure(a, b));
}

TEST_CASE("move_panel: the middle joins a group, an edge splits it") {
    DockLayout l{dock_split(false, {dock_group({"files"}), dock_group({"editor", "search"})})};
    // search into files' group
    CHECK(same_structure(move_panel(l, "search", "files", DockZone::Middle).root,
                         dock_split(false, {dock_group({"files", "search"}), dock_group({"editor"})})));
    CHECK(move_panel(l, "search", "files", DockZone::Middle).root.parts[0].active == 1);   // shown
    // search to the right of files: files' group splits side by side
    CHECK(move_panel(l, "search", "files", DockZone::Right).root ==
          dock_split(false, {dock_split(false, {dock_group({"files"}), dock_group({"search"})}), dock_group({"editor"})}));
    // search below editor, in its own group
    CHECK(move_panel(l, "search", "editor", DockZone::Bottom).root ==
          dock_split(false, {dock_group({"files"}), dock_split(true, {dock_group({"editor"}), dock_group({"search"})})}));
    // files above editor: its group, now empty, goes, and the outer split with it
    CHECK(move_panel(l, "files", "editor", DockZone::Top).root ==
          dock_split(true, {dock_group({"files"}), dock_group({"editor", "search"})}));
    // onto its own lone group: nothing to do
    CHECK(move_panel(l, "files", "files", DockZone::Left) == l);
}

TEST_CASE("the zone of a drop: the middle, or the edge it is nearest within a quarter") {
    Size s{Width{400}, Height{200}};
    auto at = [&](float x, float y) { return dock_zone(DropPlace{Point{X{x}, Y{y}}, s}); };
    CHECK(at(200, 100) == DockZone::Middle);
    CHECK(at(20, 100) == DockZone::Left);
    CHECK(at(390, 100) == DockZone::Right);
    CHECK(at(200, 10) == DockZone::Top);
    CHECK(at(200, 190) == DockZone::Bottom);
}

namespace dock_test {
struct Ide {
    Field<Label<>> files{{"FILES"}};
    Field<TextField<>> editor{{.value = "code"}};
    Field<Label<>> search{{"SEARCH"}};
    Field<DockLayout> layout{{dock_split(false, {dock_group({"files"}), dock_group({"editor", "search"})})}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.dock(layout, {
            {"files", "Files", [&](WidgetTree::ViewBuilder& p) { p.widget(files); }},
            {"editor", "Editor", [&](WidgetTree::ViewBuilder& p) { p.widget(editor); }},
            {"search", "Search", [&](WidgetTree::ViewBuilder& p) { p.widget(search); }},
        });
    }
};
}  // namespace dock_test

using namespace dock_test;

namespace {
// Where the text itself is drawn (inside a tab bar, the tab's own spot).
std::optional<Point> text_at(const SceneSnapshot& snap, std::string_view text) {
    for (auto& dl : snap.draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return t->origin;
    return std::nullopt;
}

std::optional<Rect> text_rect(const SceneSnapshot& snap, std::string_view text) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i)
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return snap.geometry[i].second;
    return std::nullopt;
}
}

TEST_CASE("the dock builds the layout: groups side by side, a group of two shows tabs") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto files = text_rect(*snap, "FILES"), code = text_rect(*snap, "code");
    REQUIRE(files);
    REQUIRE(code);
    CHECK(code->origin.x.raw() > files->origin.x.raw());   // the second group, on the right
    CHECK(text_rect(*snap, "Editor"));                      // its tab bar
    CHECK(text_rect(*snap, "Search"));
    CHECK_FALSE(text_rect(*snap, "SEARCH"));                // not the shown tab
    CHECK(text_rect(*snap, "Files"));                       // a lone panel has its tab too: its handle
}

TEST_CASE("a new layout rebuilds the dock") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    model.layout.set({dock_split(false, {dock_group({"editor", "search"}), dock_group({"files"})})});
    snap = tree.build_snapshot(600, 400, 2);
    auto files = text_rect(*snap, "FILES"), code = text_rect(*snap, "code");
    REQUIRE(files);
    REQUIRE(code);
    CHECK(files->origin.x.raw() > code->origin.x.raw());   // swapped
}

TEST_CASE("picking a tab writes it into the layout, without rebuilding the dock") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto editor_id = tree.leaf_ids();   // ids before
    auto tab = text_at(*snap, "Search");
    REQUIRE(tab);
    Point p{tab->x + DX{5}, tab->y + DY{5}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    snap = tree.build_snapshot(600, 400, 2);
    CHECK(model.layout.get().root.parts[1].active == 1);
    CHECK(text_rect(*snap, "SEARCH"));
    CHECK(tree.leaf_ids().front() == editor_id.front());   // the files panel was not rebuilt
}

TEST_CASE("dragging the split handle writes the sizes into the layout") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto files = text_rect(*snap, "FILES");
    REQUIRE(files);
    float edge = files->origin.x.raw() + files->extent.w.raw() + 3.f;   // on the handle
    Point p{X{edge}, Y{200}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{X{edge + 40}, Y{200}}});
    route_event(tree, snap.get(), MouseButton{Point{X{edge + 40}, Y{200}}, buttons::left, false});
    auto sizes = model.layout.get().root.sizes;
    REQUIRE(sizes.size() == 2);
    CHECK(sizes[0] == doctest::Approx(files->extent.w.raw() + 40.f).epsilon(0.05));
}

TEST_CASE("dragging a tab onto a group's edge moves the panel there") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto tab = text_at(*snap, "Search");
    auto files = text_rect(*snap, "FILES");
    REQUIRE(tab);
    REQUIRE(files);
    Point from{tab->x + DX{5}, tab->y + DY{5}};
    // the files group's right edge (its area spans the whole left column)
    Point to{X{files->origin.x.raw() + 0.9f * 290.f}, Y{200}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x + DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{to});
    CHECK(tree.desired_cursor() == CursorShape::Default);
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(model.layout.get().root ==
          dock_split(false, {dock_split(false, {dock_group({"files"}), dock_group({"search"})}), dock_group({"editor"})}));
    snap = tree.build_snapshot(600, 400, 2);
    CHECK(text_rect(*snap, "SEARCH"));   // shown in its new place
}

TEST_CASE("a moved panel keeps its widgets: same widget, focus and wiring") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto code = text_rect(*snap, "code");
    REQUIRE(code);
    Point p{code->origin.x + DX{5}, code->origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    auto editor_id = tree.focused_id();
    REQUIRE(editor_id != 0);
    // The editor goes to the left of the files panel: its group is rebuilt elsewhere.
    model.layout.set(move_panel(model.layout.get(), "editor", "files", DockZone::Left));
    snap = tree.build_snapshot(600, 400, 2);
    auto moved = text_rect(*snap, "code");
    REQUIRE(moved);
    CHECK(moved->origin.x.raw() < code->origin.x.raw());   // it did move
    CHECK(tree.focused_id() == editor_id);                  // the same widget, still focused
    route_event(tree, snap.get(), TextInput{"!"});
    CHECK(model.editor.get().value.find('!') != std::string::npos);   // and still wired
}

TEST_CASE("while dragging a tab: the zone is shaded, and the ghost is that tab alone") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto tab = text_at(*snap, "Search");
    auto files = text_rect(*snap, "FILES");
    Point from{tab->x + DX{5}, tab->y + DY{5}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x + DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{Point{X{files->origin.x.raw() + 0.9f * 290.f}, Y{200}}});
    snap = tree.build_snapshot(600, 400, 2);
    // The right half of the files group, shaded.
    bool shaded = false;
    for (auto& c : snap->overlay.commands)
        if (auto* r = std::get_if<FilledRect>(&c); r && r->color.a < 255 && r->rect.origin.x.raw() > 100.f) shaded = true;
    CHECK(shaded);
    // The ghost shows "Search" but not the other tab's title.
    int editor_titles = 0, search_titles = 0;
    for (auto& c : snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c)) {
            editor_titles += t->text == "Editor";
            search_titles += t->text == "Search";
        }
    CHECK(search_titles == 1);
    CHECK(editor_titles == 0);
    route_event(tree, snap.get(), KeyPress{keys::escape, 0});
}
