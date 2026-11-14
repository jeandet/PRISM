// Table cells that hold a checkbox (TableSource::cell_check; a click toggles it through
// set_cell) or an icon before their text (TableSource::cell_image).
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
// Name and a "done" box; the box column is first.
struct Todo {
    std::vector<std::string> names{"wash", "cook"};
    std::vector<bool> done{false, true};
    std::shared_ptr<const Image> dot = *Image::from_rgba(1, 1, {255, 0, 0, 255});
    size_t column_count() const { return 2; }
    size_t row_count() const { return names.size(); }
    std::string cell_text(size_t r, size_t c) const { return c == 0 ? (done[r] ? "true" : "false") : names[r]; }
    std::string_view header(size_t c) const { return c == 0 ? "Done" : "Task"; }
    std::optional<bool> cell_check(size_t r, size_t c) const { return c == 0 ? std::optional<bool>{done[r]} : std::nullopt; }
    std::shared_ptr<const Image> cell_image(size_t, size_t c) const { return c == 1 ? dot : nullptr; }
    bool set_cell(size_t r, size_t c, std::string_view t) {
        if (c == 0) done[r] = t == "true";
        else names[r] = std::string(t);
        return true;
    }
};

struct Page {
    Todo todo;
    void view(WidgetTree::ViewBuilder& vb) { vb.table(todo); }
};

struct Fixture {
    Page p;
    WidgetTree tree{p};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    TableState& ts() {
        auto find = [](auto& self, WidgetNode& n) -> WidgetNode* {
            if (n.layout_kind == LayoutKind::Table) return &n;
            for (auto& c : n.children)
                if (auto* t = self(self, c)) return t;
            return nullptr;
        };
        return **std::any_cast<std::shared_ptr<TableState>>(&find(find, tree.root())->edit_state);
    }
    float row_y(size_t r) { return ts().row_height.raw() * (static_cast<float>(r) + 1.5f); }
    float box_x() { return column_edges(ts())[0] + 12.f; }   // the box: 4 to 20 px into the cell
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    void click(float x, float y, uint8_t clicks = 1) {
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true, 0, clicks});
        route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false, 0, clicks});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    template <typename Cmd>
    std::size_t count() const {
        std::size_t n = 0;
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands) n += std::holds_alternative<Cmd>(c);
        return n;
    }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
};
}

TEST_CASE("a check cell draws a box, not its text; an image cell draws its icon") {
    Fixture f;
    CHECK_FALSE(f.shows("false"));
    CHECK(f.shows("\xe2\x9c\x93"));            // the tick in the checked row's box
    CHECK(f.count<ImageCmd>() == 2);          // one icon per task
    CHECK(f.shows("wash"));
}

TEST_CASE("a click on a box toggles it through set_cell; each click of a double-click counts") {
    Fixture f;
    f.click(f.box_x(), f.row_y(0));
    CHECK(f.p.todo.done[0]);
    f.click(f.box_x(), f.row_y(1), 2);        // the second click of a double-click
    CHECK_FALSE(f.p.todo.done[1]);
    CHECK_FALSE(f.ts().editing);              // never a text editor on a box
}

TEST_CASE("F2 and Tab skip check cells: the editor goes to the text") {
    Fixture f;
    f.click(300, f.row_y(0));                 // selects the row, on the Task cell
    CHECK_FALSE(f.p.todo.done[0]);            // not on the box: nothing toggled
    f.key(keys::f2);
    REQUIRE(f.ts().editing);
    CHECK(f.ts().editing->column == 1);
    f.key(keys::escape);
    begin_edit(f.ts(), 0, 0);
    CHECK_FALSE(f.ts().editing);
}

TEST_CASE("a read-only source shows its boxes but a click leaves them") {
    struct Shown {
        size_t column_count() const { return 1; }
        size_t row_count() const { return 1; }
        std::string cell_text(size_t, size_t) const { return "true"; }
        std::string_view header(size_t) const { return "On"; }
        std::optional<bool> cell_check(size_t, size_t) const { return true; }
    } data;
    TableState ts;
    ts.source = wrap_column_storage(data);
    ts.column_count = 1;
    CHECK(ts.source.cell_check(0, 0) == std::optional<bool>{true});
    CHECK_FALSE(toggle_check(ts, 0, 0));
}

#if __cpp_impl_reflection
namespace {
struct Chore {
    std::string name;
    bool done = false;
};
}

TEST_CASE("a bool member of a List row is a checkbox column") {
    List<Chore> chores;
    chores.push_back({"dust", false});
    TableState ts;
    ts.source = wrap_row_storage(chores);
    ts.column_count = 2;
    REQUIRE(ts.source.cell_check);
    CHECK_FALSE(ts.source.cell_check(0, 0).has_value());
    CHECK(ts.source.cell_check(0, 1) == std::optional<bool>{false});
    CHECK(toggle_check(ts, 0, 1));
    CHECK(chores[0].done);
}
#endif
