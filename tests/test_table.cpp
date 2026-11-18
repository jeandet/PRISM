#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/table.hpp>
#include <prism/ui/delegate.hpp>
#include <prism/input/hit_test.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/ui/dock.hpp>

#include <fmt/format.h>
#include <array>
#include <set>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}

TEST_CASE("LayoutKind::Table exists") {
    auto kind = prism::LayoutKind::Table;
    CHECK(kind == prism::LayoutKind::Table);
}

TEST_CASE("TableState default-constructs") {
    prism::TableState ts;
    CHECK(ts.column_count == 0);
    CHECK(ts.row_count() == 0);
    CHECK(ts.selected_row.get() == std::nullopt);
}

struct TestColumns {
    std::vector<double> time = {0.0, 0.125, 0.25};
    std::vector<std::string> label = {"SW", "SW", "MS"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return time.size(); }
    std::string_view header(size_t col) const {
        static constexpr std::array<const char*, 2> names = {"Time", "Label"};
        return names[col];
    }
    std::string cell_text(size_t row, size_t col) const {
        if (col == 0) return fmt::to_string(time[row]);
        return label[row];
    }
};

static_assert(prism::ColumnStorage<TestColumns>);

TEST_CASE("wrap_column_storage produces valid TableSource") {
    TestColumns data;
    auto src = prism::wrap_column_storage(data);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 3);
    CHECK(src.header(0) == "Time");
    CHECK(src.header(1) == "Label");
    CHECK(src.cell_text(1, 1) == "SW");
}

struct ColumnModel {
    std::vector<double> x = {1.0, 2.0, 3.0};
    std::vector<std::string> name = {"a", "b", "c"};

    size_t column_count() const { return 2; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 2> h = {"X", "Name"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        if (c == 0) return fmt::to_string(x[r]);
        return name[r];
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("ViewBuilder.table() with ColumnStorage creates Table node") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    CHECK(true);
}

TEST_CASE("Table layout: produces geometry") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    CHECK(snap->geometry.size() > 0);
    CHECK(snap->draw_lists.size() > 0);
}

TEST_CASE("Table header text appears in draw commands") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    bool found_x = false, found_name = false;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl->commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "X") found_x = true;
                if (tc->text == "Name") found_name = true;
            }
        }
    }
    CHECK(found_x);
    CHECK(found_name);
}

TEST_CASE("Table in column layout: no overlap with sibling") {
    // Mimics dashboard: scroll area + table + list in a column
    struct Model {
        prism::Field<std::string> label1{"one"};
        prism::Field<std::string> label2{"two"};
        prism::Field<std::string> label3{"three"};
        prism::Field<std::string> label4{"four"};
        prism::Field<std::string> label5{"five"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                vb.vstack(label1, label2, label3, label4, label5);
            });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    snap = tree.build_snapshot(400, 300, 2);

    // Dump all text commands with their Y positions
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl->commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                MESSAGE("text '", tc->text, "' at y=", tc->origin.y.raw());
            }
            if (auto* cp = std::get_if<prism::ClipPush>(&cmd)) {
                MESSAGE("clip_push y=", cp->rect.origin.y.raw(),
                        " h=", cp->rect.extent.h.raw());
            }
        }
    }

    // Verify: all table cell text Y positions should be >= scroll_area_height
    // The scroll gets ~100px (1/3 of 300), so table starts at ~100
    float third = 100.f;
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl->commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                // Table cell text should not be in the scroll area
                if (tc->text == "a" || tc->text == "b" || tc->text == "c") {
                    CHECK(tc->origin.y.raw() >= third - 5.f);
                }
            }
        }
    }
}

TEST_CASE("Click selects row via hit_test in multi-widget layout") {
    struct Model {
        prism::Field<std::string> label{"hello"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.widget(label); });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    Model model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(400, 300, 1);
    snap = tree.build_snapshot(400, 300, 2);

    // Find table node
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    REQUIRE(table_node != nullptr);

    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table rect: y=", table_rect->origin.y.raw(),
            " h=", table_rect->extent.h.raw());

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Click in the table body area (window coords)
    float row_h = ts.row_height.raw();
    float abs_y = table_rect->origin.y.raw() + row_h + row_h * 0.5f;  // header + half a row
    MESSAGE("clicking at abs_y=", abs_y);

    // Verify hit_test returns the table
    auto hit = prism::hit_test(*snap, prism::Point{prism::X{50.f}, prism::Y{abs_y}});
    REQUIRE(hit.has_value());
    CHECK(*hit == table_node->id);

    // Simulate what route_mouse_button does: localize and dispatch
    prism::MouseButton click{
        .position = prism::Point{prism::X{50.f}, prism::Y{abs_y}},
        .button = 1, .pressed = true};
    prism::InputEvent ev{click};
    auto localized = prism::localize_mouse(ev, prism::input::Placed{*table_rect});
    tree.dispatch(table_node->id, localized);

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 0);
}

TEST_CASE("Table selection via full route_mouse_button path") {
    // Mimic dashboard: scroll + table + list, with many fields in scroll
    struct BigModel {
        prism::Field<std::string> f1{"a"}, f2{"b"}, f3{"c"}, f4{"d"};
        prism::Field<std::string> f5{"e"}, f6{"f"}, f7{"g"}, f8{"h"};
        ColumnModel table_data;
        prism::List<std::string> list_data;

        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] { vb.vstack(f1, f2, f3, f4, f5, f6, f7, f8); });
            vb.table(table_data);
            vb.list(list_data);
        }
    };

    BigModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find table
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : tree.root().children)
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    REQUIRE(table_node != nullptr);

    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table y=", table_rect->origin.y.raw(), " h=", table_rect->extent.h.raw());

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Click in the middle of table body (absolute coords)
    float row_h = ts.row_height.raw();
    float abs_y = table_rect->origin.y.raw() + row_h + row_h * 0.5f;
    prism::Point click_pos{prism::X{50.f}, prism::Y{abs_y}};

    // What does hit_test return?
    auto hit = prism::hit_test(*snap, click_pos);
    MESSAGE("hit_test returned id=", hit.value_or(999));
    MESSAGE("table id=", table_node->id);

    // Dump ALL geometry entries that contain this point
    for (size_t i = 0; i < snap->geometry.size(); ++i) {
        auto& [id, rect] = snap->geometry[i];
        if (rect.contains(click_pos)) {
            MESSAGE("geometry[", i, "] id=", id,
                    " y=", rect.origin.y.raw(), " h=", rect.extent.h.raw());
        }
    }

    REQUIRE(hit.has_value());
    CHECK(*hit == table_node->id);
}

TEST_CASE("Cell text position is relative inside clip") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find a cell text command for column 1 (Name) — should be inside
    // its column's clip rect, not double-offset
    for (auto& dl : snap->draw_lists) {
        for (auto& cmd : dl->commands) {
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) {
                if (tc->text == "a" || tc->text == "b" || tc->text == "c") {
                    // Text x should be within a reasonable range
                    // (column_origin + small padding), not double-offset
                    CHECK(tc->origin.x.raw() < 600.f);
                }
            }
        }
    }
}

TEST_CASE("Click on table row sets selected_row") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table) {
            table_node = &c;
            break;
        }
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    float row_h = ts.row_height.raw();
    float header_h = row_h;
    float click_y = header_h + row_h * 1.5f;

    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 1);
}

TEST_CASE("hit_test finds table, not cells") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    // Find the table node to get its ID
    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    // Find table rect in geometry
    auto table_rect = prism::find_widget_rect(*snap, table_node->id);
    REQUIRE(table_rect.has_value());
    MESSAGE("table rect: y=", table_rect->origin.y.raw(),
            " h=", table_rect->extent.h.raw());

    // Click in the middle of the table body
    float mid_y = table_rect->origin.y.raw() + table_rect->extent.h.raw() * 0.5f;
    float mid_x = table_rect->origin.x.raw() + table_rect->extent.w.raw() * 0.5f;
    auto hit_id = prism::hit_test(*snap, prism::Point{prism::X{mid_x}, prism::Y{mid_y}});
    REQUIRE(hit_id.has_value());
    CHECK(*hit_id == table_node->id);

    // Now dispatch a localized click on row 1 and verify selection works
    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    float row_h = ts.row_height.raw();
    float header_h = row_h;
    // local_y = header + 1.5 rows → row index 1
    float local_y = header_h + row_h * 1.5f;
    MESSAGE("local_y=", local_y, " row_h=", row_h, " row_count=", ts.row_count());
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{local_y}},
        .button = 1, .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});
    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 1);
}

TEST_CASE("Click selected row deselects") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    ts.selected_row.set(std::optional<size_t>{1});

    float row_h = ts.row_height.raw();
    float click_y = row_h + row_h * 1.5f;
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get() == std::nullopt);
}

TEST_CASE("Arrow keys move selection") {
    ColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Arrow down with no selection -> select row 0
    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});

    // Arrow down -> row 1
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get() == std::optional<size_t>{1});

    // Arrow up -> row 0
    prism::KeyPress up_key{.key = prism::keys::up, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{up_key});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});

    // Arrow up at row 0 -> stays at 0
    tree.dispatch(table_node->id, prism::InputEvent{up_key});
    CHECK(ts.selected_row.get() == std::optional<size_t>{0});
}

struct LargeColumnModel {
    std::vector<double> values;

    LargeColumnModel() : values(100) {
        for (size_t i = 0; i < 100; ++i) values[i] = static_cast<double>(i);
    }

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Value"; }
    std::string cell_text(size_t r, size_t) const { return fmt::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this);
    }
};

TEST_CASE("Mouse wheel scrolls table vertically") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    CHECK(ts.scroll_y.raw() == 0.f);

    prism::MouseScroll scroll{
        .position = prism::Point{prism::X{10.f}, prism::Y{100.f}},
        .dx = prism::DX{0}, .dy = prism::DY{3.f}};
    tree.scroll_at(table_node->id, prism::DY{scroll.dy.raw() * 60.f});  // what routing does

    CHECK(ts.scroll_y.raw() > 0.f);
}

TEST_CASE("Arrow key selection scrolls into view") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Select a row near the bottom of viewport, then press down past it
    size_t visible_rows = ts.viewport_h.raw() > 0
        ? static_cast<size_t>(ts.viewport_h.raw() / ts.row_height.raw())
        : 10;
    ts.selected_row.set(std::optional<size_t>{visible_rows - 1});

    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});

    CHECK(ts.selected_row.get().value() == visible_rows);
    CHECK(ts.scroll_y.raw() > 0.f);
}

struct DynamicColumnModel {
    std::vector<double> values = {1.0, 2.0};
    prism::Field<bool> data_updated{false};

    size_t column_count() const { return 1; }
    size_t row_count() const { return values.size(); }
    std::string_view header(size_t) const { return "Val"; }
    std::string cell_text(size_t r, size_t) const { return fmt::to_string(values[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).depends_on(data_updated);
    }
};

TEST_CASE("ColumnStorage table re-renders on depends_on trigger") {
    DynamicColumnModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.data_updated.set(true);
    CHECK(tree.any_dirty());
}

// A query result: its shape changes with each query.
struct ResultModel {
    std::vector<std::string> headers{"count"};
    prism::Field<int> generation{0};

    size_t column_count() const { return headers.size(); }
    size_t rows = 2;
    size_t row_count() const { return rows; }
    std::string_view header(size_t c) const { return headers[c]; }
    std::string cell_text(size_t r, size_t c) const { return headers[c] + std::to_string(r); }

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.table(*this).depends_on(generation); }
};

TEST_CASE("a table whose source gains columns shows them on the next frame") {
    ResultModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.headers = {"borough", "trips", "avg_fare"};
    model.generation.set(1);
    (void)tree.build_snapshot(800, 600, 3);
    auto snap = tree.build_snapshot(800, 600, 4);

    std::set<std::string> texts;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) texts.insert(tc->text);
    CHECK(texts.contains("borough"));
    CHECK(texts.contains("avg_fare"));
    CHECK(texts.contains("avg_fare1"));
}

TEST_CASE("a table that starts with no columns and no rows shows the first result") {
    ResultModel model;
    model.headers.clear();
    model.rows = 0;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.headers = {"hour", "trips"};
    model.rows = 24;
    model.generation.set(1);
    auto snap = tree.build_snapshot(800, 600, 3);   // the very frame the result arrives in

    std::set<std::string> texts;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) texts.insert(tc->text);
    CHECK(texts.contains("hour"));
    CHECK(texts.contains("trips"));
    CHECK(texts.contains("hour3"));
}

TEST_CASE("a result arriving in a table on a tab page in a dock shows all its rows and columns") {
    struct Studio {
        ResultModel result;
        prism::Field<prism::TabBar<>> pages{};
        prism::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(true,
            {prism::ui::dock_group({"editor"}), prism::ui::dock_group({"results"})}, {0.4f, 0.6f})}};
        prism::Field<prism::Label<>> editor{{"SQL"}};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.dock(layout, {{"editor", "Query", [&](prism::WidgetTree::ViewBuilder& p) { p.widget(editor); }},
                             {"results", "Results", [&](prism::WidgetTree::ViewBuilder& p) {
                                 p.tabs(pages, [&] {
                                     p.tab("Table", [&](prism::WidgetTree::ViewBuilder& page) {
                                         page.table(result).depends_on(result.generation);
                                     });
                                 });
                             }}});
        }
    } model;
    model.result.headers.clear();
    model.result.rows = 0;
    prism::WidgetTree tree(model);
    for (uint64_t v = 1; v < 4; ++v) { (void)tree.build_snapshot(800, 600, v); tree.clear_dirty(); }

    model.result.headers = {"hour", "trips"};
    model.result.rows = 24;
    model.result.generation.set(1);
    std::unique_ptr<prism::SceneSnapshot> snap;
    for (uint64_t v = 4; v < 7; ++v) { snap = tree.build_snapshot(800, 600, v); tree.clear_dirty(); }

    std::set<std::string> texts;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) texts.insert(tc->text);
    CHECK(texts.contains("hour"));
    CHECK(texts.contains("trips"));
    CHECK(texts.contains("hour0"));
    CHECK(texts.contains("hour5"));
}

TEST_CASE("a table whose panel grows shows the rows that now fit") {
    struct Panes {
        ResultModel result;
        prism::Field<prism::Label<>> top{{"TOP"}};
        prism::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(true,
            {prism::ui::dock_group({"top"}), prism::ui::dock_group({"table"})}, {0.85f, 0.15f})}};
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.dock(layout, {{"top", "Top", [&](prism::WidgetTree::ViewBuilder& p) { p.widget(top); }},
                             {"table", "Table", [&](prism::WidgetTree::ViewBuilder& p) {
                                 p.table(result).depends_on(result.generation);
                             }}});
        }
    } model;
    model.result.headers = {"hour"};
    model.result.rows = 40;
    prism::WidgetTree tree(model);
    for (uint64_t v = 1; v < 4; ++v) { (void)tree.build_snapshot(800, 600, v); tree.clear_dirty(); }

    std::unique_ptr<prism::SceneSnapshot> snap;   // the window grows: so does the table's panel
    for (uint64_t v = 4; v < 7; ++v) { snap = tree.build_snapshot(800, 3000, v); tree.clear_dirty(); }

    std::set<std::string> texts;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd)) texts.insert(tc->text);
    CHECK(texts.contains("hour10"));   // 450 px of rows now: well past the few the small panel held
}

struct HeaderOverrideModel {
    std::vector<double> x = {1.0, 2.0};

    size_t column_count() const { return 1; }
    size_t row_count() const { return x.size(); }
    std::string_view header(size_t) const { return "X"; }
    std::string cell_text(size_t r, size_t) const { return fmt::to_string(x[r]); }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(*this).headers({"Custom"});
    }
};

TEST_CASE("headers() builder overrides table headers") {
    HeaderOverrideModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;
    REQUIRE(ts.header_overrides.size() == 1);
    CHECK(ts.header_overrides[0] == "Custom");
}

TEST_CASE("Full table workflow: render, scroll, select, observe") {
    LargeColumnModel model;
    prism::WidgetTree tree(model);

    // Frame 1-2: stabilize
    (void)tree.build_snapshot(800, 600, 1);
    auto snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children) {
        if (c.layout_kind == prism::LayoutKind::Table)
            table_node = &c;
    }
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    auto& ts = **sp;

    // Verify initial state
    CHECK(ts.row_count() == 100);
    CHECK(ts.selected_row.get() == std::nullopt);
    CHECK(ts.scroll_y.raw() == 0.f);

    // Observe selection changes
    std::optional<size_t> observed_row;
    ts.selected_row.observe([&](const std::optional<size_t>& r) {
        observed_row = r;
    });

    // Select row via click
    float row_h = ts.row_height.raw();
    float header_h = row_h;
    float click_y = header_h + row_h * 5.5f; // click on row 5
    prism::MouseButton click{
        .position = prism::Point{prism::X{10.f}, prism::Y{click_y}},
        .button = 1,
        .pressed = true};
    tree.dispatch(table_node->id, prism::InputEvent{click});

    CHECK(ts.selected_row.get().has_value());
    CHECK(ts.selected_row.get().value() == 5);
    CHECK(observed_row.has_value());
    CHECK(observed_row.value() == 5);

    // Scroll down via mouse wheel
    prism::MouseScroll scroll{
        .position = prism::Point{prism::X{10.f}, prism::Y{100.f}},
        .dx = prism::DX{0}, .dy = prism::DY{5.f}};
    tree.scroll_at(table_node->id, prism::DY{scroll.dy.raw() * 60.f});  // what routing does
    CHECK(ts.scroll_y.raw() > 0.f);

    // Re-render after scroll — should still produce valid output
    snap = tree.build_snapshot(800, 600, 3);
    CHECK(snap->geometry.size() > 0);
    CHECK(snap->draw_lists.size() > 0);

    // Arrow key moves selection
    prism::KeyPress down{.key = prism::keys::down, .mods = 0};
    tree.dispatch(table_node->id, prism::InputEvent{down});
    CHECK(ts.selected_row.get().value() == 6);
    CHECK(observed_row.value() == 6);
}

#if __cpp_impl_reflection
#include <prism/core/list.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/core/reflect_leaf.hpp>


struct TestRow {
    prism::Field<std::string> label{""};
    prism::Field<double> value{0.0};
};

TEST_CASE("wrap_row_storage produces valid TableSource") {
    prism::List<TestRow> rows;
    rows.push_back(TestRow{.label = {"Alpha"}, .value = {1.5}});
    rows.push_back(TestRow{.label = {"Beta"}, .value = {2.7}});

    auto src = prism::wrap_row_storage(rows);
    CHECK(src.column_count() == 2);
    CHECK(src.row_count() == 2);
    CHECK(src.header(0) == "label");
    CHECK(src.header(1) == "value");
    CHECK(src.cell_text(0, 0) == "Alpha");
    CHECK(src.cell_text(1, 1) == fmt::to_string(2.7));
}

struct RowModel {
    prism::List<TestRow> rows;

    RowModel() {
        rows.push_back(TestRow{.label = {"A"}, .value = {1.0}});
        rows.push_back(TestRow{.label = {"B"}, .value = {2.0}});
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(rows);
    }
};

TEST_CASE("ViewBuilder.table() with RowStorage creates Table node") {
    RowModel model;
    prism::WidgetTree tree(model);
    CHECK(true);
}

TEST_CASE("RowStorage table updates on List insert") {
    RowModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.rows.push_back(TestRow{.label = {"C"}, .value = {3.0}});
    CHECK(tree.any_dirty());
}

TEST_CASE("RowStorage table updates on List remove") {
    RowModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.rows.erase(0);
    CHECK(tree.any_dirty());
}

namespace {
prism::ui::TableState& test_table_state(prism::WidgetTree& tree) {
    auto find = [](auto& self, prism::WidgetNode& n) -> prism::WidgetNode* {
        if (n.layout_kind == prism::LayoutKind::Table) return &n;
        for (auto& c : n.children)
            if (auto* t = self(self, c)) return t;
        return nullptr;
    };
    auto* n = find(find, tree.root());
    REQUIRE(n);
    return **std::any_cast<std::shared_ptr<prism::ui::TableState>>(&n->edit_state);
}
}

TEST_CASE("a sorted List table follows updates, appends and removals") {
    RowModel model;
    for (int i = 0; i < 40; ++i) model.rows.push_back(TestRow{.label = {"r"}, .value = {double((i * 7) % 13)}});
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    auto& ts = test_table_state(tree);
    ts.sort.set(prism::ui::SortSpec{1, true});
    auto frame = [&, v = std::uint64_t{2}]() mutable { (void)tree.build_snapshot(800, 600, v++); };
    auto full_sort = [&] { return prism::ui::sort_order(ts.source, prism::ui::SortSpec{1, true}); };
    frame();
    CHECK(ts.order == full_sort());
    model.rows.set(3, TestRow{.label = {"x"}, .value = {50.0}});
    frame();
    CHECK(ts.order == full_sort());
    model.rows.push_back(TestRow{.label = {"y"}, .value = {-1.0}});
    frame();
    CHECK(ts.order == full_sort());
    model.rows.erase(0);
    frame();
    CHECK(ts.order == full_sort());
    CHECK(ts.order.front() == 2);   // row 3 before the removal, now the largest
}

struct PlainRow {
    int id = 0;
    [[=prism::core::skip]] int hidden = 0;
    std::string name;
    [[=prism::core::label<"Score %">]] float score = 0.f;
};

TEST_CASE("wrap_row_storage supports plain (non-Field) struct rows") {
    prism::List<PlainRow> rows;
    rows.push_back(PlainRow{.id = 1, .hidden = 99, .name = "Alpha", .score = 1.5f});
    rows.push_back(PlainRow{.id = 2, .hidden = 42, .name = "Beta", .score = 2.5f});

    auto src = prism::wrap_row_storage(rows);
    CHECK(src.column_count() == 3);   // hidden excluded via skip
    CHECK(src.row_count() == 2);
    CHECK(src.header(0) == "id");
    CHECK(src.header(1) == "name");
    CHECK(src.header(2) == "Score %"); // label override
    CHECK(src.cell_text(0, 0) == "1");
    CHECK(src.cell_text(0, 1) == "Alpha");
    CHECK(src.cell_text(1, 2) == fmt::to_string(2.5f));
}

struct PlainRowModel {
    prism::List<PlainRow> rows;
    PlainRowModel() {
        rows.push_back(PlainRow{.id = 1, .hidden = 0, .name = "A", .score = 1.f});
    }
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(rows);
    }
};

TEST_CASE("ViewBuilder.table() with a plain-struct List still creates a Table node") {
    PlainRowModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    bool found_header = false;
    for (auto& dl : snap->draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* tc = std::get_if<prism::TextCmd>(&cmd))
                if (tc->text == "Score %") found_header = true;
    CHECK(found_header);
}

struct SoaColumns {
    std::vector<int> id = {1, 2, 3};
    std::vector<std::string> name = {"a", "b", "c"};
    [[=prism::core::label<"Score %">]] std::vector<float> score = {1.5f, 2.5f, 3.5f};
    [[=prism::core::skip]] std::vector<int> hidden = {9, 9, 9};
    prism::Field<int> revision{0}; // not a vector -- silently excluded, doesn't disqualify the shape
};

static_assert(prism::SoaStorage<SoaColumns>);
static_assert(!prism::SoaStorage<prism::List<int>>);
static_assert(!prism::SoaStorage<PlainRow>); // PlainRow (Task 5) has no vector members at all

TEST_CASE("wrap_soa_columns produces valid TableSource from struct-of-vectors") {
    SoaColumns cols;
    auto src = prism::wrap_soa_columns(cols);
    CHECK(src.column_count() == 3); // hidden excluded via skip, revision isn't a vector column
    CHECK(src.row_count() == 3);
    CHECK(src.header(0) == "id");
    CHECK(src.header(1) == "name");
    CHECK(src.header(2) == "Score %");
    CHECK(src.cell_text(1, 1) == "b");
    CHECK(src.cell_text(2, 2) == fmt::to_string(3.5f));
}

struct SoaTableModel {
    SoaColumns cols;
    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(cols);
    }
};

TEST_CASE("ViewBuilder.table() auto-detects a struct-of-vectors and creates a Table node") {
    SoaTableModel model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);

    auto& root = tree.root();
    prism::WidgetNode* table_node = nullptr;
    for (auto& c : root.children)
        if (c.layout_kind == prism::LayoutKind::Table) table_node = &c;
    REQUIRE(table_node != nullptr);

    auto* sp = std::any_cast<std::shared_ptr<prism::TableState>>(&table_node->edit_state);
    REQUIRE(sp);
    CHECK((*sp)->row_count() == 3);
}

TEST_CASE("SOA table re-renders on depends_on trigger") {
    struct RevisionModel {
        SoaColumns cols;
        void view(prism::WidgetTree::ViewBuilder& vb) {
            vb.table(cols).depends_on(cols.revision);
        }
    };
    RevisionModel model;
    prism::WidgetTree tree(model);
    (void)tree.build_snapshot(800, 600, 1);
    (void)tree.build_snapshot(800, 600, 2);
    tree.clear_dirty();

    model.cols.revision.set(1);
    CHECK(tree.any_dirty());
}
#endif // __cpp_impl_reflection
