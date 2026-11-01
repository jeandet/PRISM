#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/table_ops.hpp>
#include <prism/app/table_input.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/app/event_routing.hpp>

#include <array>
#include <string>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app;
}
using namespace prism;

namespace {

struct Procs {
    std::vector<std::string> name = {"bash", "Xorg", "audio", "zsh"};
    std::vector<std::string> mem  = {"12", "300", "9.5", "100"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return name.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 2> h = {"Name", "Mem"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const { return c == 0 ? name[r] : mem[r]; }

    void view(WidgetTree::ViewBuilder& vb) { vb.table(*this); }
};

TableState make_state(Procs& p) {
    TableState ts;
    ts.source = wrap_column_storage(p);
    ts.column_count = p.column_count();
    ts.row_height = Height{24.f};
    ts.viewport_h = Height{240.f};
    ts.viewport_w = Width{400.f};
    ts.auto_column_w = 100.f;
    return ts;
}

InputEvent press(float x, float y, uint16_t m = 0) {
    return MouseButton{.position = Point{X{x}, Y{y}}, .button = 1, .pressed = true, .mods = m};
}
InputEvent release(float x, float y) {
    return MouseButton{.position = Point{X{x}, Y{y}}, .button = 1, .pressed = false};
}
InputEvent move(float x, float y) {
    return MouseMove{.position = Point{X{x}, Y{y}}, .buttons = 1};
}
InputEvent key(int32_t k, uint16_t m = 0) { return KeyPress{.key = k, .mods = m}; }

float row_y(size_t view_pos) { return 24.f + 24.f * static_cast<float>(view_pos) + 12.f; }

} // namespace

TEST_CASE("compare_cells orders numbers by value, text by bytes, numbers first") {
    CHECK(compare_cells("9.5", "12") < 0);
    CHECK(compare_cells("100", "12") > 0);
    CHECK(compare_cells("-3", "2") < 0);
    CHECK(compare_cells("abc", "abd") < 0);
    CHECK(compare_cells("7", "abc") < 0);
    CHECK(compare_cells("12", "12") == 0);
    CHECK(compare_cells("12 MB", "9 MB") > 0);
}

TEST_CASE("sort_order is stable and honours direction") {
    Procs p;
    p.mem = {"5", "1", "5", "3"};
    auto src = wrap_column_storage(p);
    CHECK(sort_order(src, SortSpec{1, false}) == std::vector<size_t>{1, 3, 0, 2});
    CHECK(sort_order(src, SortSpec{1, true}) == std::vector<size_t>{0, 2, 3, 1});
}

TEST_CASE("next_sort: new column ascending, same column toggles") {
    CHECK(next_sort(std::nullopt, 1) == SortSpec{1, false});
    CHECK(next_sort(SortSpec{1, false}, 1) == SortSpec{1, true});
    CHECK(next_sort(SortSpec{1, true}, 1) == SortSpec{1, false});
    CHECK(next_sort(SortSpec{1, true}, 0) == SortSpec{0, false});
}

TEST_CASE("column edges honour resized widths and the auto width") {
    Procs p;
    auto ts = make_state(p);
    CHECK(column_edges(ts) == std::vector<float>{0.f, 100.f, 200.f});
    ts.column_widths = {150.f};
    auto e = column_edges(ts);
    CHECK(e == std::vector<float>{0.f, 150.f, 250.f});
    CHECK(column_at(e, 10.f) == 0u);
    CHECK(column_at(e, 160.f) == 1u);
    CHECK(column_at(e, 260.f) == std::nullopt);
    CHECK(resize_handle_at(e, 148.f) == 0u);
    CHECK(resize_handle_at(e, 253.f) == 1u);
    CHECK(resize_handle_at(e, 100.f) == std::nullopt);
    CHECK(resize_handle_at(e, 1.f) == std::nullopt);
}

TEST_CASE("clicking a header sorts; the view maps through the order") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, press(150.f, 10.f));  // "Mem" header
    refresh_order(ts);
    REQUIRE(ts.sort.get() == SortSpec{1, false});
    // mem = 12, 300, 9.5, 100 -> audio(2), bash(0), zsh(3), Xorg(1)
    CHECK(source_row(ts, 0) == 2u);
    CHECK(source_row(ts, 3) == 1u);
    CHECK(view_position(ts, 1) == 3u);

    handle_table_input(ts, press(150.f, 10.f));
    refresh_order(ts);
    CHECK(ts.sort.get() == SortSpec{1, true});
    CHECK(source_row(ts, 0) == 1u);
}

TEST_CASE("selection is kept in data rows, so it follows a re-sort") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, press(10.f, row_y(1)));  // Xorg
    CHECK(ts.selected_row.get() == 1u);
    ts.sort.set(SortSpec{1, true});
    refresh_order(ts);
    CHECK(is_selected(ts, 1));
    CHECK(view_position(ts, 1) == 0u);
}

TEST_CASE("ctrl-click toggles, shift-click selects the visible range") {
    Procs p;
    auto ts = make_state(p);
    ts.sort.set(SortSpec{1, false});  // view: audio(2), bash(0), zsh(3), Xorg(1)
    refresh_order(ts);

    handle_table_input(ts, press(10.f, row_y(0)));
    handle_table_input(ts, press(10.f, row_y(2), mods::ctrl));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{2, 3});

    handle_table_input(ts, press(10.f, row_y(2), mods::ctrl));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{2});

    handle_table_input(ts, press(10.f, row_y(1)));
    handle_table_input(ts, press(10.f, row_y(3), mods::shift));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{0, 1, 3});
    CHECK(ts.selected_row.get() == 1u);
}

TEST_CASE("shift+arrows extend from the anchor; ctrl+a selects all; home/end jump") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, key(keys::down));
    handle_table_input(ts, key(keys::down, mods::shift));
    handle_table_input(ts, key(keys::down, mods::shift));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{0, 1, 2});
    handle_table_input(ts, key(keys::up, mods::shift));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{0, 1});

    handle_table_input(ts, key(keys::a, mods::ctrl));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{0, 1, 2, 3});

    handle_table_input(ts, key(keys::end));
    CHECK(ts.selected_row.get() == 3u);
    CHECK(ts.selected_rows.get() == std::vector<size_t>{3});
    handle_table_input(ts, key(keys::home));
    CHECK(ts.selected_row.get() == 0u);
}

TEST_CASE("dragging a header edge resizes that column, with a floor") {
    Procs p;
    auto ts = make_state(p);
    auto r = handle_table_input(ts, move(99.f, 10.f));
    CHECK(r.cursor == CursorShape::ResizeEW);

    handle_table_input(ts, press(99.f, 10.f));
    CHECK(ts.sort.get() == std::nullopt);  // grabbing an edge doesn't sort
    handle_table_input(ts, move(159.f, 300.f));
    CHECK(column_width(ts, 0) == doctest::Approx(160.f));
    handle_table_input(ts, move(-500.f, 10.f));
    CHECK(column_width(ts, 0) == doctest::Approx(min_column_w));
    handle_table_input(ts, release(-500.f, 10.f));
    handle_table_input(ts, move(159.f, 10.f));
    CHECK(column_width(ts, 0) == doctest::Approx(min_column_w));
}

TEST_CASE("horizontal scroll is clamped to the column overflow") {
    Procs p;
    auto ts = make_state(p);
    ts.column_widths = {300.f, 300.f};  // 600 wide in a 400 viewport
    handle_table_input(ts, MouseScroll{.position = Point{X{10.f}, Y{50.f}}, .dx = DX{10.f}, .dy = DY{0.f}});
    CHECK(ts.scroll_x.raw() == doctest::Approx(200.f));
    // header hit-testing accounts for the scroll: x=150 is now inside column 1
    handle_table_input(ts, press(150.f, 10.f));
    CHECK(ts.sort.get() == SortSpec{1, false});
}

TEST_CASE("WidgetTree: data change re-sorts a sorted table") {
    Procs p;
    WidgetTree tree(p);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    WidgetNode* table = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == LayoutKind::Table) table = &c;
    if (!table && tree.root().layout_kind == LayoutKind::Table) table = &tree.root();
    REQUIRE(table);
    auto& ts = **std::any_cast<std::shared_ptr<TableState>>(&table->edit_state);

    ts.sort.set(SortSpec{0, false});
    (void)tree.build_snapshot(800, 600, 3);
    CHECK(source_row(ts, 0) == 1u);  // "Xorg" < "audio" bytewise

    p.name.push_back("Aardvark");
    p.mem.push_back("1");
    (void)tree.build_snapshot(800, 600, 4);
    CHECK(source_row(ts, 0) == 4u);
}

// ---- Findings from the opencode review of 04b03e3 ----

TEST_CASE("a resize ends when a move arrives with the button already up") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, press(99.f, 10.f));
    REQUIRE(ts.resizing);
    // The release was lost (pointer left the window): plain hover must not keep resizing.
    auto hover = MouseMove{.position = Point{X{180.f}, Y{100.f}}, .buttons = 0};
    auto r = handle_table_input(ts, hover);
    CHECK_FALSE(ts.resizing);
    CHECK(column_width(ts, 0) == doctest::Approx(100.f));
    CHECK(r.cursor == CursorShape::Default);
}

TEST_CASE("ctrl-clicking a row off unselects it, and a plain click then selects") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, press(10.f, row_y(0)));
    handle_table_input(ts, press(10.f, row_y(1), mods::ctrl));
    handle_table_input(ts, press(10.f, row_y(1), mods::ctrl));
    CHECK_FALSE(is_selected(ts, 1));
    CHECK(is_selected(ts, 0));

    handle_table_input(ts, press(10.f, row_y(0), mods::ctrl));  // nothing left
    CHECK_FALSE(is_selected(ts, 0));
    handle_table_input(ts, press(10.f, row_y(0)));
    CHECK(is_selected(ts, 0));
}

TEST_CASE("selection drops rows that no longer exist") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, key(keys::a, mods::ctrl));
    handle_table_input(ts, key(keys::end));
    handle_table_input(ts, press(10.f, row_y(1), mods::ctrl));
    p.name.resize(2);
    p.mem.resize(2);
    refresh_order(ts);
    CHECK(ts.selected_rows.get() == std::vector<size_t>{1});
    CHECK(ts.selected_row.get() == 1u);

    handle_table_input(ts, key(keys::end));  // current = 1 (last row)
    p.name.resize(1);
    p.mem.resize(1);
    refresh_order(ts);
    CHECK(ts.selected_rows.get().empty());
    CHECK(ts.selected_row.get() == std::nullopt);
    CHECK(ts.anchor_row == std::nullopt);
}

TEST_CASE("ctrl+a sets a current row, so shift+arrows extend from it") {
    Procs p;
    auto ts = make_state(p);
    handle_table_input(ts, key(keys::a, mods::ctrl));
    CHECK(ts.selected_row.get() == 0u);
    handle_table_input(ts, key(keys::down, mods::shift));
    CHECK(ts.selected_rows.get() == std::vector<size_t>{0, 1});
}

TEST_CASE("leading_number accepts tabs and an explicit plus") {
    CHECK(leading_number("\t5") == 5.0);
    CHECK(leading_number("+5") == 5.0);
    CHECK(leading_number("+") == std::nullopt);
    CHECK(compare_cells("+5", "-5") > 0);
}

TEST_CASE("one wheel notch scrolls a table once, through the shared scroll path") {
    struct Many {
        size_t column_count() const { return 1; }
        size_t row_count() const { return 1000; }
        std::string_view header(size_t) const { return "N"; }
        std::string cell_text(size_t r, size_t) const { return std::to_string(r); }
        void view(WidgetTree::ViewBuilder& vb) { vb.table(*this); }
    } m;
    WidgetTree tree(m);
    (void)tree.build_snapshot(800, 600, 1);
    auto snap = tree.build_snapshot(800, 600, 2);
    WidgetNode* table = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == LayoutKind::Table) table = &c;
    REQUIRE(table);
    auto& ts = **std::any_cast<std::shared_ptr<TableState>>(&table->edit_state);

    widget_detail::route_mouse_scroll(tree, *snap, MouseScroll{.position = Point{X{100.f}, Y{300.f}}, .dx = DX{0}, .dy = DY{1.f}});
    CHECK(ts.scroll_y.raw() == doctest::Approx(60.f));  // the shared wheel step, not 60 + 3 rows
}
