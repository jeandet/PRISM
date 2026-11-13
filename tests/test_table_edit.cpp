// Editing a table's cells in place: TableSource::set_cell, double-click or F2 to start, Enter
// to commit, Escape to cancel, Tab to the next cell.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <charconv>
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
// Two columns, name and quantity; a quantity must be a number.
struct Stock {
    std::vector<std::string> names{"apples", "pears"};
    std::vector<int> counts{3, 5};
    bool editable = true;
    size_t column_count() const { return 2; }
    size_t row_count() const { return names.size(); }
    std::string cell_text(size_t r, size_t c) const { return c == 0 ? names[r] : std::to_string(counts[r]); }
    std::string_view header(size_t c) const { return c == 0 ? "Name" : "Count"; }
    bool set_cell(size_t r, size_t c, std::string_view text) {
        if (c == 0) { names[r] = std::string(text); return true; }
        int v = 0;
        auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec != std::errc{} || p != text.data() + text.size()) return false;
        counts[r] = v;
        return true;
    }
};

struct Page {
    Stock stock;
    void view(WidgetTree::ViewBuilder& vb) { vb.table(stock); }
};

// The header is the first row; col_x and row_y give a cell's middle.
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
    float col_x(size_t c) { auto e = column_edges(ts()); return (e[c] + e[c + 1]) / 2; }
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    void double_click(float x, float y) {
        for (uint8_t clicks : {uint8_t{1}, uint8_t{2}}) {
            route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, true, 0, clicks});
            route_event(tree, snap.get(), MouseButton{Point{X{x}, Y{y}}, buttons::left, false, 0, clicks});
        }
        frame();
    }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); frame(); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
};
}

TEST_CASE("double-click a cell, type, Enter: the source gets the new text") {
    Fixture f;
    f.double_click(f.col_x(1), f.row_y(0));   // apples' count
    REQUIRE(f.ts().editing);
    f.type("12");                      // replaces the whole text, selected when editing starts
    CHECK(f.shows("12"));
    f.key(keys::enter);
    CHECK_FALSE(f.ts().editing);
    CHECK(f.p.stock.counts[0] == 12);
}

TEST_CASE("Escape leaves the cell as it was") {
    Fixture f;
    f.double_click(f.col_x(0), f.row_y(1));
    f.type("plums");
    f.key(keys::escape);
    CHECK_FALSE(f.ts().editing);
    CHECK(f.p.stock.names[1] == "pears");
}

TEST_CASE("text the source refuses keeps the editor open, marked") {
    Fixture f;
    f.double_click(f.col_x(1), f.row_y(1));
    f.type("many");
    f.key(keys::enter);
    REQUIRE(f.ts().editing);
    CHECK(f.ts().editing->rejected);
    CHECK(f.p.stock.counts[1] == 5);
    f.key(keys::backspace);
    f.key(keys::backspace);
    f.key(keys::backspace);
    f.key(keys::backspace);
    f.type("7");
    f.key(keys::enter);
    CHECK_FALSE(f.ts().editing);
    CHECK(f.p.stock.counts[1] == 7);
}

TEST_CASE("F2 edits the selected row; Tab commits and moves to the next cell") {
    Fixture f;
    route_event(f.tree, f.snap.get(), MouseButton{Point{X{40}, Y{f.row_y(0)}}, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseButton{Point{X{40}, Y{f.row_y(0)}}, buttons::left, false});
    f.frame();
    f.key(keys::f2);
    REQUIRE(f.ts().editing);
    CHECK(f.ts().editing->column == 0);
    f.type("figs");
    f.key(keys::tab);
    CHECK(f.p.stock.names[0] == "figs");
    REQUIRE(f.ts().editing);
    CHECK(f.ts().editing->column == 1);
    CHECK(f.ts().editing->row == 0);
}

TEST_CASE("a source without set_cell is read-only") {
    struct ReadOnly {
        size_t column_count() const { return 1; }
        size_t row_count() const { return 1; }
        std::string cell_text(size_t, size_t) const { return "fixed"; }
        std::string_view header(size_t) const { return "Value"; }
    };
    struct M {
        ReadOnly data;
        void view(WidgetTree::ViewBuilder& vb) { vb.table(data); }
    } m;
    CHECK_FALSE(wrap_column_storage(m.data).set_cell);
}

#if __cpp_impl_reflection
namespace {
struct Item {
    std::string name;
    double price = 0;
};
struct Shop {
    List<Item> items;
    void view(WidgetTree::ViewBuilder& vb) { vb.table(items); }
};
}

TEST_CASE("a table over a List of plain structs writes each column in its own type") {
    Shop s;
    s.items.push_back({"tea", 2.5});
    auto src = wrap_row_storage(s.items);
    REQUIRE(src.set_cell);
    CHECK(src.set_cell(0, 1, "3.75"));
    CHECK(s.items[0].price == doctest::Approx(3.75));
    CHECK_FALSE(src.set_cell(0, 1, "cheap"));   // not a number: refused, unchanged
    CHECK(s.items[0].price == doctest::Approx(3.75));
    CHECK(src.set_cell(0, 0, "green tea"));
    CHECK(s.items[0].name == "green tea");
}
#endif

namespace {
// Only the second column takes edits.
struct Notes {
    std::vector<std::string> notes{"", ""};
    size_t column_count() const { return 2; }
    size_t row_count() const { return 2; }
    std::string cell_text(size_t r, size_t c) const { return c == 0 ? "row " + std::to_string(r) : notes[r]; }
    std::string_view header(size_t c) const { return c == 0 ? "Id" : "Note"; }
    bool editable(size_t c) const { return c == 1; }
    bool set_cell(size_t r, size_t, std::string_view t) { notes[r] = std::string(t); return true; }
};
}

TEST_CASE("read-only columns are skipped: no editor on them, F2 and Tab go to the editable ones") {
    struct M {
        Notes data;
        void view(WidgetTree::ViewBuilder& vb) { vb.table(data); }
    } m;
    auto src = wrap_column_storage(m.data);
    REQUIRE(src.column_editable);
    TableState ts;
    ts.source = src;
    ts.column_count = 2;
    begin_edit(ts, 0, 0);
    CHECK_FALSE(ts.editing);            // the Id column takes no edits
    CHECK(first_editable_column(ts) == std::optional<size_t>{1});
    begin_edit(ts, 0, 1);
    CHECK(ts.editing);
}

TEST_CASE("editing a cell out of view scrolls the table to it") {
    struct Wide {
        size_t column_count() const { return 3; }
        size_t row_count() const { return 1; }
        std::string cell_text(size_t, size_t) const { return "x"; }
        std::string_view header(size_t) const { return "h"; }
        bool set_cell(size_t, size_t, std::string_view) { return true; }
    } data;
    TableState ts;
    ts.source = wrap_column_storage(data);
    ts.column_count = 3;
    ts.column_widths = {120, 120, 120};
    ts.viewport_w = Width{200};
    begin_edit(ts, 0, 2);
    CHECK(ts.scroll_x.raw() == doctest::Approx(160.f));   // the third column's right edge, in view
    begin_edit(ts, 0, 0);
    CHECK(ts.scroll_x.raw() == doctest::Approx(0.f));
}
