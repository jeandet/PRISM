// vb.table(...).selection(rows): the app reads the rows the user selected, and selects rows itself.
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
struct Cols {
    std::vector<std::string> name = {"a", "b", "c", "d"};
    size_t column_count() const { return 1; }
    size_t row_count() const { return name.size(); }
    std::string_view header(size_t) const { return "Name"; }
    std::string cell_text(size_t r, size_t) const { return name[r]; }
};
struct Doc {
    Cols data;
    Field<std::vector<size_t>> picked;
    void view(WidgetTree::ViewBuilder& vb) { vb.table(data).selection(picked); }
};

WidgetNode* find_table(WidgetNode& node) {
    if (node.layout_kind == LayoutKind::Table) return &node;
    for (auto& c : node.children)
        if (auto* t = find_table(c)) return t;
    return nullptr;
}
TableState& table_state(WidgetTree& tree) {
    auto* n = find_table(tree.root());
    REQUIRE(n);
    return **std::any_cast<std::shared_ptr<TableState>>(&n->edit_state);
}
void click(WidgetTree& tree, const SceneSnapshot* snap, float y) {
    route_event(tree, snap, MouseButton{Point{X{20}, Y{y}}, buttons::left, true});
    route_event(tree, snap, MouseButton{Point{X{20}, Y{y}}, buttons::left, false});
}
} // namespace

TEST_CASE("the app's field holds the rows the user selects") {
    Doc doc;
    WidgetTree tree{doc};
    auto snap = tree.build_snapshot(400, 300, 1);
    float h = table_state(tree).row_height.raw();
    click(tree, snap.get(), h * 2.5f);   // header, then rows 0, 1: this is row 1
    CHECK(doc.picked.get() == std::vector<size_t>{1});
}

TEST_CASE("the app selects rows by setting its field") {
    Doc doc;
    WidgetTree tree{doc};
    auto snap = tree.build_snapshot(400, 300, 1);
    doc.picked = std::vector<size_t>{0, 3};
    auto& ts = table_state(tree);
    CHECK(is_selected(ts, 0));
    CHECK(is_selected(ts, 3));
    CHECK_FALSE(is_selected(ts, 1));
    doc.picked = std::vector<size_t>{};
    CHECK_FALSE(is_selected(ts, 0));
    CHECK_FALSE(is_selected(ts, 3));
}
