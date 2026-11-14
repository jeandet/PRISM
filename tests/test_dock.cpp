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

TEST_CASE("the guide: five buttons in a cross at the group's centre, not overlapping") {
    Size s{Width{400}, Height{200}};
    auto guide = dock_guide(s);
    CHECK(guide[0].first == DockZone::Middle);
    CHECK(guide[0].second.center() == Point{X{200}, Y{100}});   // the group's centre
    CHECK(guide[0].second.extent.w.raw() == doctest::Approx(32.f));
    for (std::size_t i = 0; i < guide.size(); ++i) {
        [[maybe_unused]] auto& [zone, r] = guide[i];
        // every button inside the group
        CHECK(r.origin.x.raw() >= 0.f);
        CHECK(r.extent.w.raw() > 0.f);
        CHECK(r.origin.x.raw() + r.extent.w.raw() <= s.w.raw());
        CHECK(r.origin.y.raw() + r.extent.h.raw() <= s.h.raw());
        for (std::size_t j = i + 1; j < guide.size(); ++j) {
            auto cut = r.intersect(guide[j].second);
            CHECK(cut.extent.w.raw() * cut.extent.h.raw() == 0.f);
        }
    }
    // Left/Right in the middle row, Top/Bottom in the middle column.
    CHECK(guide[1].first == DockZone::Left);
    CHECK(guide[2].first == DockZone::Right);
    CHECK(guide[3].first == DockZone::Top);
    CHECK(guide[4].first == DockZone::Bottom);
    CHECK(guide[1].second.center().y.raw() == doctest::Approx(100.f));
    CHECK(guide[2].second.center().y.raw() == doctest::Approx(100.f));
    CHECK(guide[3].second.center().x.raw() == doctest::Approx(200.f));
    CHECK(guide[4].second.center().x.raw() == doctest::Approx(200.f));
    // A small group still gets a cross that fits.
    auto small = dock_guide(Size{Width{90}, Height{90}});
    CHECK(small[4].second.origin.y.raw() + small[4].second.extent.h.raw() <= 90.f);
}

TEST_CASE("a drop on a guide button is that button's zone, even inside the middle region") {
    Size s{Width{400}, Height{200}};
    auto guide = dock_guide(s);
    auto at = [&](Point p) { return dock_zone(DropPlace{p, s}); };
    for (auto& [zone, r] : guide) CHECK(at(r.center()) == zone);
    // The Left button is nowhere near the group's edge: the old rule would say Middle there.
    Point left = guide[1].second.center();
    CHECK(left.x.raw() > s.w.raw() * 0.25f);
    CHECK(left.y.raw() > s.h.raw() * 0.25f);
    CHECK(left.y.raw() < s.h.raw() * 0.75f);
    // Between the buttons, the edge rule still decides.
    CHECK(at(Point{X{200}, Y{40}}) == DockZone::Top);
    CHECK(at(Point{X{200}, Y{100}}) == DockZone::Middle);
    CHECK(at(Point{X{30}, Y{100}}) == DockZone::Left);
}

TEST_CASE("dock_panel_at_edge: a new group along that whole side of the dock") {
    DockLayout l{dock_split(false, {dock_group({"files"}), dock_group({"editor", "search"})})};
    CHECK(dock_panel_at_edge(l, "search", DockZone::Left).root ==
          dock_split(false, {dock_group({"search"}),
                             dock_split(false, {dock_group({"files"}), dock_group({"editor"})})}));
    CHECK(dock_panel_at_edge(l, "files", DockZone::Top).root ==
          dock_split(true, {dock_group({"files"}), dock_group({"editor", "search"})}));
    // The dock's only panel: nothing to split it with.
    DockLayout lone{dock_group({"a"})};
    CHECK(dock_panel_at_edge(lone, "a", DockZone::Right) == lone);
    // An unknown, or floating, panel is left where it is.
    CHECK(dock_panel_at_edge(l, "gone", DockZone::Left) == l);
    DockLayout floating{dock_split(false, {dock_group({"files"}), dock_group({"editor"})}), {"search"}};
    CHECK(dock_panel_at_edge(floating, "search", DockZone::Left) == floating);
}

TEST_CASE("the edge buttons: one at the middle of each of the dock's sides") {
    Size s{Width{600}, Height{400}};
    auto guide = dock_edge_guide(s);
    REQUIRE(guide.size() == 4);
    CHECK(guide[0].first == DockZone::Left);
    CHECK(guide[0].second.center() == Point{X{24}, Y{200}});
    CHECK(guide[1].first == DockZone::Right);
    CHECK(guide[1].second.center() == Point{X{576}, Y{200}});
    CHECK(guide[2].first == DockZone::Top);
    CHECK(guide[2].second.center() == Point{X{300}, Y{24}});
    CHECK(guide[3].first == DockZone::Bottom);
    CHECK(guide[3].second.center() == Point{X{300}, Y{376}});
    // Anywhere else on the dock: no edge button.
    CHECK_FALSE(dock_edge_zone(DropPlace{Point{X{300}, Y{200}}, s}));
    CHECK(dock_edge_zone(DropPlace{Point{X{24}, Y{200}}, s}) == DockZone::Left);
    CHECK(dock_edge_zone(DropPlace{Point{X{300}, Y{376}}, s}) == DockZone::Bottom);
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

// The drop targets as the tree has them: a group (the one showing `title`'s tab) or the dock.
template <typename Pred>
std::optional<Rect> drop_rect(WidgetTree& tree, WidgetNode& n, Pred&& pred) {
    if (pred(n))
        if (auto r = tree.drop_area_rect(n.id)) return r;
    for (auto& c : n.children)
        if (auto r = drop_rect(tree, c, pred)) return r;
    return std::nullopt;
}

std::optional<Rect> group_rect(WidgetTree& tree, const SceneSnapshot& snap, std::string_view title) {
    auto tab = text_at(snap, title);
    if (!tab) return std::nullopt;
    return drop_rect(tree, tree.root(), [&](WidgetNode& n) {
        auto r = n.drag.zones ? tree.drop_area_rect(n.id) : std::nullopt;
        return r && r->contains(*tab);
    });
}

std::optional<Rect> dock_rect(WidgetTree& tree) {
    return drop_rect(tree, tree.root(), [](WidgetNode& n) { return n.drag.edge_zones; });
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

TEST_CASE("dropping a tab on a group's guide button splits the group that way") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto tab = text_at(*snap, "Search");
    auto group = group_rect(tree, *snap, "Files");
    REQUIRE(tab);
    REQUIRE(group);
    auto left = dock_guide(group->extent)[1].second;   // the Left button
    Point to{group->origin.x + DX{left.center().x.raw()}, group->origin.y + DY{left.center().y.raw()}};
    Point from{tab->x + DX{5}, tab->y + DY{5}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x + DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{to});
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(model.layout.get().root ==
          dock_split(false, {dock_split(false, {dock_group({"search"}), dock_group({"files"})}),
                             dock_group({"editor"})}));
}

TEST_CASE("dropping a tab on the dock's edge button docks it along that whole side") {
    Ide model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(600, 400, 1);
    auto tab = text_at(*snap, "Search");
    auto dock = dock_rect(tree);
    REQUIRE(tab);
    REQUIRE(dock);
    auto left = dock_edge_guide(dock->extent)[0].second;
    Point to{dock->origin.x + DX{left.center().x.raw()}, dock->origin.y + DY{left.center().y.raw()}};
    // The button lies over the files group: it still gets the drop, not the group under it.
    auto files_group = group_rect(tree, *snap, "Files");
    REQUIRE(files_group);
    CHECK(files_group->contains(to));
    Point from{tab->x + DX{5}, tab->y + DY{5}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x + DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{to});
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(model.layout.get().root ==
          dock_split(false, {dock_group({"search"}),
                             dock_split(false, {dock_group({"files"}), dock_group({"editor"})})}));
    snap = tree.build_snapshot(600, 400, 2);
    auto search = text_rect(*snap, "SEARCH"), files = text_rect(*snap, "FILES");
    REQUIRE(search);
    REQUIRE(files);
    CHECK(search->origin.x.raw() < files->origin.x.raw());   // a column of its own, on the left
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
    // The drop guide is there too: a button per zone over the group, four on the dock's edges.
    int rounded = 0;
    for (auto& c : snap->overlay.commands) rounded += std::holds_alternative<RoundedRect>(c);
    CHECK(rounded >= 9);
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

// --- floating panels ---------------------------------------------------------------------

TEST_CASE("a floating panel is out of the dock's groups; an unknown one is dropped") {
    DockLayout l{dock_split(false, {dock_group({"files"}), dock_group({"editor"})}), {"search", "gone"}};
    auto fit = fitted(l, {"files", "editor", "search"});
    CHECK(fit.floating == std::vector<std::string>{"search"});
    CHECK(fit.root == dock_split(false, {dock_group({"files"}), dock_group({"editor"})}));   // not added back
    // A panel both docked and floating counts as docked.
    DockLayout both{dock_group({"files", "search"}), {"search"}};
    CHECK(fitted(both, {"files", "search"}).floating.empty());
}

namespace {
// Stands in for AppContext: records what the dock asks for.
struct Windows {
    std::vector<std::string> opened;
    std::vector<WidgetId> closed;
    std::vector<std::function<void()>> on_closed;
    void install(WidgetTree& tree) {
        tree.set_window_opener(
            [this](WidgetTree::WindowRequest r) {
                opened.push_back(r.title);
                on_closed.push_back(r.on_closed);
                if (r.on_opened) r.on_opened(static_cast<prism::app::WindowId>(100 + opened.size()));
            },
            [this](prism::app::WindowId id) { closed.push_back(id); });
    }
};
}

TEST_CASE("a tab dropped outside the window floats its panel in a window of its own") {
    Ide model;
    WidgetTree tree{model};
    Windows windows;
    windows.install(tree);
    auto snap = tree.build_snapshot(600, 400, 1);
    auto tab = text_at(*snap, "Search");
    Point from{tab->x + DX{5}, tab->y + DY{5}};
    Point outside{X{700}, Y{200}};   // past the window's right edge
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x + DX{10}, from.y}});
    route_event(tree, snap.get(), MouseMove{outside});
    route_event(tree, snap.get(), MouseButton{outside, buttons::left, false});
    CHECK(model.layout.get().floating == std::vector<std::string>{"search"});
    snap = tree.build_snapshot(600, 400, 2);
    CHECK(windows.opened == std::vector<std::string>{"Search"});
    CHECK_FALSE(text_at(*snap, "Search"));   // no longer a tab here
    // Closing its window docks it again.
    windows.on_closed.front()();
    snap = tree.build_snapshot(600, 400, 3);
    CHECK(model.layout.get().floating.empty());
    CHECK(text_at(*snap, "Search"));
}

TEST_CASE("docking a floating panel from code closes its window; no opener keeps it docked") {
    Ide model;
    model.layout.set(float_panel(model.layout.get(), "search"));
    {
        WidgetTree tree{model};
        Windows windows;
        windows.install(tree);
        auto snap = tree.build_snapshot(600, 400, 1);
        REQUIRE(windows.opened.size() == 1);
        model.layout.set(dock_panel(model.layout.get(), "search"));
        snap = tree.build_snapshot(600, 400, 2);
        CHECK(windows.closed == std::vector<WidgetId>{101});
    }
    model.layout.set(float_panel(model.layout.get(), "search"));
    WidgetTree plain{model};   // no window opener (a test, the browser)
    auto snap = plain.build_snapshot(600, 400, 1);
    snap = plain.build_snapshot(600, 400, 2);
    CHECK(model.layout.get().floating.empty());   // it stays in the dock
    CHECK(text_at(*snap, "Search"));
}
