// A list's selected row (vb.list(items).selection(field)): a click selects, the row shows it,
// and removals keep it on the same item.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <optional>
#include <string>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Shopping {
    List<int> items;
    Field<std::optional<std::size_t>> selected{std::nullopt};
    Shopping() {
        for (int v : {10, 20, 30, 40}) items.push_back(v);
    }
    void view(WidgetTree::ViewBuilder& vb) { vb.list(items).selection(selected); }
};

struct Fixture {
    Shopping m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(300, 300, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(300, 300, ++v); }
    Rect row(std::size_t i) const {
        // rows in order on screen, top to bottom
        std::vector<Rect> rows;
        for (auto& [id, r] : snap->geometry)
            if (r.extent.h.raw() > 0 && r.extent.h.raw() < 60) rows.push_back(r);
        std::ranges::sort(rows, {}, [](const Rect& r) { return r.origin.y.raw(); });
        return rows.at(i);
    }
    void click_row(std::size_t i) {
        auto c = row(i).center();
        route_event(tree, snap.get(), MouseButton{c, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{c, buttons::left, false});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    bool highlighted(std::size_t i) const {
        auto r = row(i);
        auto sel = tree.theme().text_selection;
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* f = std::get_if<FilledRect>(&c); f && f->color == sel && r.contains(f->rect.center())) return true;
        return false;
    }
};
}

TEST_CASE("a click selects the row, and the row shows it") {
    Fixture f;
    f.click_row(2);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{2});
    CHECK(f.highlighted(2));
    CHECK_FALSE(f.highlighted(1));
    f.click_row(0);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
    CHECK_FALSE(f.highlighted(2));
}

TEST_CASE("a removal keeps the selection on the same item, or clears it") {
    Fixture f;
    f.m.selected.set(2);   // the app selects too
    f.m.items.erase(0);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{1});   // still 30
    f.m.items.erase(1);
    CHECK_FALSE(f.m.selected.get().has_value());                   // 30 itself is gone
}

TEST_CASE("Up and Down move the selection; Home and End jump; the ends stop it") {
    Fixture f;
    f.click_row(1);
    f.key(keys::down);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{2});
    f.key(keys::up);
    f.key(keys::up);
    f.key(keys::up);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
    f.key(keys::end);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{3});
    f.key(keys::down);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{3});
    f.key(keys::home);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
    CHECK(f.highlighted(0));
}

TEST_CASE("Tab reaches the list; Down with nothing selected selects the first row") {
    Fixture f;
    f.key(keys::tab);
    f.key(keys::down);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
}

TEST_CASE("a row moved to by key scrolls into view") {
    struct Long {
        List<int> items;
        Field<std::optional<std::size_t>> selected{std::nullopt};
        Long() {
            for (int v = 0; v < 100; ++v) items.push_back(v);
        }
        void view(WidgetTree::ViewBuilder& vb) { vb.list(items).selection(selected); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(300, 300, 1);
    route_event(tree, snap.get(), KeyPress{keys::tab, 0});
    route_event(tree, snap.get(), KeyPress{keys::end, 0});
    snap = tree.build_snapshot(300, 300, 2);
    CHECK(m.selected.get() == std::optional<std::size_t>{99});
    bool shows_last = false;
    for (auto& dl : snap->draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "99") shows_last = true;
    CHECK(shows_last);
}

TEST_CASE("a moved item keeps the selection on it; moves around it shift it") {
    Fixture f;
    f.m.selected.set(1);           // 20
    f.m.items.move(3, 0);          // 40 10 20 30
    CHECK(f.m.selected.get() == std::optional<std::size_t>{2});
    f.m.items.move(2, 0);          // the selected item itself: 20 40 10 30
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
    f.m.items.move(1, 3);          // after it: no change
    CHECK(f.m.selected.get() == std::optional<std::size_t>{0});
}

TEST_CASE("Up from a selection past the end lands on the last row") {
    Fixture f;
    f.m.selected.set(10);
    f.key(keys::tab);
    f.key(keys::up);
    CHECK(f.m.selected.get() == std::optional<std::size_t>{3});
}
