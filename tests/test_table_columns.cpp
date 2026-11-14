// A table's columns: which ones show, in what order (TableState::columns, or the app's own field
// through vb.table(...).columns(field)); dragging a header moves its column.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/table_input.hpp>
#include <prism/app/widget_tree.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::handle_table_input;
using prism::app::widget_detail::route_event;

namespace {
// Three columns: A, B, C; cell text "<column letter><row>".
struct Abc {
    size_t column_count() const { return 3; }
    size_t row_count() const { return 2; }
    std::string cell_text(size_t r, size_t c) const { return std::string(1, char('A' + c)) + std::to_string(r); }
    std::string_view header(size_t c) const { return c == 0 ? "A" : c == 1 ? "B" : "C"; }
    bool set_cell(size_t, size_t, std::string_view) { return true; }
};

TableState make_state(Abc& d) {
    TableState ts;
    ts.source = wrap_column_storage(d);
    ts.column_count = 3;
    ts.row_height = Height{24.f};
    ts.viewport_h = Height{240.f};
    ts.viewport_w = Width{300.f};
    ts.auto_column_w = 100.f;
    return ts;
}

InputEvent press(float x, float y) { return MouseButton{.position = Point{X{x}, Y{y}}, .button = 1, .pressed = true}; }
InputEvent release(float x, float y) { return MouseButton{.position = Point{X{x}, Y{y}}, .button = 1, .pressed = false}; }
InputEvent move(float x, float y) { return MouseMove{.position = Point{X{x}, Y{y}}, .buttons = 1}; }
}

TEST_CASE("by default every column shows, in the source's order") {
    Abc d;
    auto ts = make_state(d);
    CHECK(shown_columns(ts) == std::vector<size_t>{0, 1, 2});
}

TEST_CASE("hidden and reordered columns: the edges, hit tests and widths follow the shown ones") {
    Abc d;
    auto ts = make_state(d);
    set_column_width(ts, 2, 150.f);             // C keeps its width wherever it goes
    columns_field(ts).set({2, 0});                    // C then A; B hidden
    CHECK(column_edges(ts) == std::vector<float>{0.f, 150.f, 250.f});
    handle_table_input(ts, press(20.f, 10.f));   // the first header shown is C
    handle_table_input(ts, release(20.f, 10.f));
    CHECK(ts.sort.get() == SortSpec{2, false});
}

TEST_CASE("Tab while editing goes to the next column on screen") {
    Abc d;
    auto ts = make_state(d);
    columns_field(ts).set({2, 0, 1});
    CHECK(first_editable_column(ts) == std::optional<size_t>{2});
    CHECK(next_editable_column(ts, 2, +1) == std::optional<size_t>{0});
    CHECK(next_editable_column(ts, 1, +1) == std::nullopt);   // B is last on screen
}

TEST_CASE("dragging a header moves its column; it does not sort") {
    Abc d;
    auto ts = make_state(d);
    handle_table_input(ts, press(50.f, 10.f));    // A
    handle_table_input(ts, move(140.f, 10.f));
    handle_table_input(ts, move(260.f, 10.f));    // past the middle of C
    handle_table_input(ts, release(260.f, 10.f));
    CHECK(shown_columns(ts) == std::vector<size_t>{1, 2, 0});
    CHECK_FALSE(ts.sort.get());
}

TEST_CASE("a small wobble during a header click still sorts") {
    Abc d;
    auto ts = make_state(d);
    handle_table_input(ts, press(150.f, 10.f));
    handle_table_input(ts, move(152.f, 11.f));
    handle_table_input(ts, release(152.f, 11.f));
    CHECK(ts.sort.get() == SortSpec{1, false});
    CHECK(shown_columns(ts) == std::vector<size_t>{0, 1, 2});
}

namespace {
struct Page {
    Abc data;
    Field<std::vector<size_t>> columns{{0, 2}};   // B hidden by the app
    void view(WidgetTree::ViewBuilder& vb) { vb.table(data).columns(columns); }
};
bool shows(const SceneSnapshot& s, std::string_view text) {
    for (auto& dl : s.draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
    return false;
}
}

TEST_CASE("the app's own field picks the columns; a change shows at once") {
    Page p;
    WidgetTree tree{p};
    auto snap = tree.build_snapshot(400, 200, 1);
    CHECK(shows(*snap, "A0"));
    CHECK_FALSE(shows(*snap, "B0"));
    p.columns.set({1});
    snap = tree.build_snapshot(400, 200, 2);
    CHECK(shows(*snap, "B0"));
    CHECK_FALSE(shows(*snap, "A0"));
}
