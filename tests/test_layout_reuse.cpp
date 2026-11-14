// The layout tree is kept between publishes: a subtree with nothing changed is moved over from
// the last one instead of being rebuilt and measured again. Whatever is reused, the frame must
// come out as a full rebuild would make it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <array>
#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace {
struct Rows {
    std::array<Field<Label<>>, 40> names{};
    std::array<Field<bool>, 40> flags{};
    List<int> extra;
    Rows() {
        for (std::size_t i = 0; i < names.size(); ++i) names[i].set(Label<>{"row " + std::to_string(i)});
    }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {
            for (std::size_t i = 0; i < names.size(); ++i) vb.hstack(names[i], flags[i]);
            vb.list(extra);
        });
    }
};

std::vector<std::pair<WidgetId, Rect>> geometry(const SceneSnapshot& s) {
    std::vector<std::pair<WidgetId, Rect>> out(s.geometry.begin(), s.geometry.end());
    return out;
}

// What a tree built from scratch shows for the same model.
std::vector<std::pair<WidgetId, Rect>> fresh_geometry(Rows& m) {
    WidgetTree t{m};
    auto s = t.build_snapshot(400, 300, 1);
    t.clear_dirty();
    s = t.build_snapshot(400, 300, 2);
    return geometry(*s);
}

Rect rect_showing(const SceneSnapshot& s, std::string_view text) {
    for (std::size_t i = 0; i < s.draw_lists.size(); ++i)
        for (auto& c : s.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return s.geometry[i].second;
    return {};
}
}

TEST_CASE("an unchanged frame reuses the layout and comes out the same") {
    Rows m;
    WidgetTree tree{m};
    auto first = tree.build_snapshot(400, 300, 1);
    CHECK(first->layout_nodes_reused == 0);
    tree.clear_dirty();
    first = tree.build_snapshot(400, 300, 2);   // widgets re-recorded at their size: measured again
    tree.clear_dirty();
    auto second = tree.build_snapshot(400, 300, 3);
    CHECK(second->layout_nodes_reused >= 120);   // 40 rows of a row, a label and a checkbox
    CHECK(geometry(*second) == geometry(*first));
}

TEST_CASE("a changed widget is laid out again; its neighbours are reused") {
    Rows m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    snap = tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();
    float before = rect_showing(*snap, "row 3").extent.w.raw();
    m.names[3].set(Label<>{"row 3, with a much longer name"});
    snap = tree.build_snapshot(400, 300, 3);
    tree.clear_dirty();
    CHECK(rect_showing(*snap, "row 3, with a much longer name").extent.w.raw() > before);
    CHECK(snap->layout_nodes_reused >= 100);
    CHECK(geometry(*snap) == fresh_geometry(m));
}

TEST_CASE("rows added to a list show up, and the rest is still reused") {
    Rows m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    m.extra.push_back(7);
    m.extra.push_back(8);
    snap = tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();
    snap = tree.build_snapshot(400, 300, 3);
    tree.clear_dirty();
    CHECK(geometry(*snap) == fresh_geometry(m));
}

TEST_CASE("a resize lays the reused nodes out at the new size") {
    Rows m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    snap = tree.build_snapshot(250, 300, 2);
    tree.clear_dirty();
    snap = tree.build_snapshot(250, 300, 3);
    WidgetTree fresh{m};
    auto f = fresh.build_snapshot(250, 300, 1);
    fresh.clear_dirty();
    f = fresh.build_snapshot(250, 300, 2);
    CHECK(geometry(*snap) == geometry(*f));
}
