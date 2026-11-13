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
    CHECK_FALSE(text_rect(*snap, "Files"));                 // a lone panel has no tab bar
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
