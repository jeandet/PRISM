// Several selected nodes in a tree (TreeController::selection): Ctrl-click toggles, Shift-click
// and Shift+arrows select a range in the order the rows show; `selected` stays the current one.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/tree.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::ui;

namespace {
// Four roots, no children: ids 1..4.
TreeSource flat() {
    return {.root_count = [] { return std::size_t{4}; },
            .root_at = [](std::size_t i) { return static_cast<TreeNodeId>(i + 1); },
            .child_count = [](TreeNodeId) { return std::size_t{0}; },
            .child_at = [](TreeNodeId, std::size_t) { return TreeNodeId{0}; },
            .label = [](TreeNodeId id) { return "node " + std::to_string(id); },
            .has_children = [](TreeNodeId) { return false; }};
}

std::vector<TreeNodeId> highlighted(const TreeController& c) {
    std::vector<TreeNodeId> out;
    for (std::size_t i = 0; i < c.rows.size(); ++i)
        if (c.rows[i].selected) out.push_back(c.rows[i].id);
    return out;
}

void click(TreeController& c, std::size_t i, uint16_t m = 0) { c.on_row_clicked(i, c.rows[i], m); }
}

TEST_CASE("a plain click selects one node; Ctrl-click adds and removes nodes") {
    TreeController c{flat()};
    click(c, 0);
    CHECK(c.selection.get() == std::vector<TreeNodeId>{1});
    click(c, 2, mods::ctrl);
    CHECK(c.selection.get() == std::vector<TreeNodeId>{1, 3});
    CHECK(c.selected.get() == std::optional<TreeNodeId>{3});   // the current one
    CHECK(highlighted(c) == std::vector<TreeNodeId>{1, 3});
    click(c, 0, mods::ctrl);
    CHECK(c.selection.get() == std::vector<TreeNodeId>{3});
    click(c, 1);
    CHECK(c.selection.get() == std::vector<TreeNodeId>{2});
}

TEST_CASE("Shift-click selects the range from the last click, in the rows' order") {
    TreeController c{flat()};
    click(c, 3);
    click(c, 1, mods::shift);
    CHECK(c.selection.get() == std::vector<TreeNodeId>{2, 3, 4});
    CHECK(highlighted(c) == std::vector<TreeNodeId>{2, 3, 4});
}

TEST_CASE("Shift+arrows extend the selection; a plain arrow selects one node again") {
    TreeController c{flat()};
    click(c, 1);
    c.on_key(KeyPress{keys::down, mods::shift});
    c.on_key(KeyPress{keys::down, mods::shift});
    CHECK(c.selection.get() == std::vector<TreeNodeId>{2, 3, 4});
    c.on_key(KeyPress{keys::up, 0});
    CHECK(c.selection.get() == std::vector<TreeNodeId>{3});
}
