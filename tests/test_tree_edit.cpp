// Tree nodes with a checkbox (TreeSource::checked/set_checked) and nodes renamed in place
// (TreeSource::rename): F2 or a double-click opens an editor, Enter commits, Escape cancels.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <map>
#include <string>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
// Three roots, no children. Node 3 has no checkbox; names must not be empty.
struct Tasks {
    std::map<TreeNodeId, std::string> names{{1, "wash"}, {2, "cook"}, {3, "rest"}};
    std::map<TreeNodeId, bool> done{{1, false}, {2, true}};
    TreeSource source() {
        return {.root_count = [] { return std::size_t{3}; },
                .root_at = [](std::size_t i) { return static_cast<TreeNodeId>(i + 1); },
                .child_count = [](TreeNodeId) { return std::size_t{0}; },
                .child_at = [](TreeNodeId, std::size_t) { return TreeNodeId{0}; },
                .label = [this](TreeNodeId id) { return names.at(id); },
                .has_children = [](TreeNodeId) { return false; },
                .checked = [this](TreeNodeId id) -> std::optional<bool> {
                    if (auto it = done.find(id); it != done.end()) return it->second;
                    return std::nullopt;
                },
                .set_checked = [this](TreeNodeId id, bool on) { done[id] = on; },
                .rename = [this](TreeNodeId id, std::string_view to) {
                    if (to.empty()) return false;
                    names[id] = std::string(to);
                    return true;
                }};
    }
};

struct Page {
    Tasks tasks;
    TreeController ctrl{tasks.source()};
    void view(WidgetTree::ViewBuilder& vb) { vb.tree(ctrl, false); }
};

struct Fixture {
    Page p;
    WidgetTree tree{p};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(300, 200, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(300, 200, ++v); }
    static float row_y(std::size_t i) { return Widget<TreeRow>::row_h.raw() * (static_cast<float>(i) + 0.5f); }
    void click(float x, float y, uint8_t clicks = 1) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true, 0, clicks});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false, 0, clicks});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
};
}

TEST_CASE("rows carry their checkbox state; a node without one has none") {
    Page p;
    REQUIRE(p.ctrl.rows.size() == 3);
    CHECK(p.ctrl.rows[0].checked == std::optional<bool>{false});
    CHECK(p.ctrl.rows[1].checked == std::optional<bool>{true});
    CHECK_FALSE(p.ctrl.rows[2].checked.has_value());
}

TEST_CASE("a click on the box toggles it, without selecting the node") {
    Fixture f;
    f.click(Widget<TreeRow>::check_x(f.p.ctrl.rows[0]) + 8, Fixture::row_y(0));
    CHECK(f.p.tasks.done[1]);
    CHECK(f.p.ctrl.rows[0].checked == std::optional<bool>{true});
    CHECK(f.p.ctrl.selection.get().empty());
    f.click(200, Fixture::row_y(0));   // on the label: selects, the box stays
    CHECK(f.p.tasks.done[1]);
    CHECK(f.p.ctrl.selection.get() == std::vector<TreeNodeId>{1});
}

TEST_CASE("Space toggles the current node's box") {
    Fixture f;
    f.click(200, Fixture::row_y(1));
    f.key(keys::space);
    CHECK_FALSE(f.p.tasks.done[2]);
}

TEST_CASE("F2 renames the current node: type, Enter") {
    Fixture f;
    f.click(200, Fixture::row_y(0));
    f.key(keys::f2);
    REQUIRE(f.p.ctrl.renaming());
    f.type("scrub");              // the whole name is selected when the editor opens
    CHECK(f.shows("scrub"));
    f.key(keys::enter);
    CHECK_FALSE(f.p.ctrl.renaming());
    CHECK(f.p.tasks.names[1] == "scrub");
    CHECK(f.p.ctrl.rows[0].label == "scrub");
}

TEST_CASE("Escape keeps the old name; a refused name keeps the editor open, marked") {
    Fixture f;
    f.click(200, Fixture::row_y(2), 2);   // a double-click opens the editor too
    REQUIRE(f.p.ctrl.renaming());
    f.type("nap");
    f.key(keys::escape);
    CHECK(f.p.tasks.names[3] == "rest");

    f.key(keys::f2);
    f.key(keys::backspace);       // empty: refused
    f.key(keys::enter);
    REQUIRE(f.p.ctrl.renaming());
    CHECK(f.p.ctrl.rows[2].rename->rejected);
    CHECK(f.p.tasks.names[3] == "rest");
}

TEST_CASE("while renaming, the keys go to the editor, not to the tree") {
    Fixture f;
    f.click(200, Fixture::row_y(0));
    f.key(keys::f2);
    f.key(keys::down);            // would move the current node
    f.key(keys::tab);             // would move the focus
    CHECK(f.p.ctrl.selected.get() == std::optional<TreeNodeId>{1});
    CHECK(f.p.ctrl.renaming());
}

TEST_CASE("a tree without rename opens no editor") {
    Tasks t;
    auto src = t.source();
    src.rename = nullptr;
    TreeController c{src};
    c.on_row_clicked(0, c.rows[0]);
    c.on_key(KeyPress{keys::f2, 0});
    CHECK_FALSE(c.renaming());
}
