// The layout tree is kept between publishes: a subtree with nothing changed is moved over from
// the last one instead of being rebuilt and measured again. Whatever is reused, the frame must
// come out as a full rebuild would make it.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <algorithm>
#include <any>
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

TEST_CASE("a publish that lays out twice reports nothing kept: the second pass built it all") {
    Rows m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 300, 1);
    tree.clear_dirty();
    snap = tree.build_snapshot(400, 300, 2);
    tree.clear_dirty();
    snap = tree.build_snapshot(400, 200, 3);   // the scroll view's viewport shrinks: a second pass
    REQUIRE(snap->layout_pass_count > 1);
    CHECK(snap->layout_nodes_reused == 0);
}

// The passes after layout skip what was kept. Whatever they look for must still be found, and
// whatever moved must still be written back.
namespace {
WidgetNode* find_node(WidgetNode& n, auto&& pred) {
    if (pred(n)) return &n;
    for (auto& c : n.children)
        if (auto* p = find_node(c, pred)) return p;
    return nullptr;
}

WidgetNode* showing(WidgetNode& root, std::string_view text) {
    return find_node(root, [&](WidgetNode& n) {
        return std::ranges::any_of(n.draws.commands, [&](auto& c) {
            auto* t = std::get_if<TextCmd>(&c);
            return t && t->text == text;
        });
    });
}

struct Settled {
    WidgetTree& tree;
    uint64_t v = 0;
    std::unique_ptr<SceneSnapshot> snap;
    explicit Settled(WidgetTree& t) : tree(t) {
        for (int i = 0; i < 3; ++i) frame();
    }
    void frame() {
        snap = tree.build_snapshot(400, 300, ++v);
        tree.clear_dirty();
    }
};
}

TEST_CASE("scrolling moves the kept rows on screen") {
    Rows m;
    WidgetTree tree{m};
    Settled s{tree};
    auto* row = showing(tree.root(), "row 10");
    REQUIRE(row);
    Y before = row->absolute_y;
    Rect drawn_before = rect_showing(*s.snap, "row 6");
    REQUIRE(drawn_before.extent.w.raw() > 0);   // on screen before and after
    prism::app::widget_detail::route_event(tree, s.snap.get(),
        input::MouseScroll{Point{X{100}, Y{100}}, DX{0}, DY{-1}});   // down, towards the user
    s.frame();
    CHECK(s.snap->layout_nodes_reused >= 100);
    CHECK(row->absolute_y < before);
    CHECK(rect_showing(*s.snap, "row 6").origin.y < drawn_before.origin.y);
    s.frame();
    CHECK(row->absolute_y < before);
    CHECK(rect_showing(*s.snap, "row 6").origin.y < drawn_before.origin.y);
}

TEST_CASE("rows that move down are placed again") {
    struct Grows {
        List<int> head;
        std::array<Field<Label<>>, 20> names{};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.vstack([&] {
                vb.list(head);
                for (auto& n : names) vb.widget(n);
            });
        }
    } m;
    for (std::size_t i = 0; i < m.names.size(); ++i) m.names[i].set(Label<>{"name " + std::to_string(i)});
    WidgetTree tree{m};
    Settled s{tree};
    m.head.push_back(1);
    m.head.push_back(2);
    s.frame();
    s.frame();
    WidgetTree fresh{m};
    Settled f{fresh};
    CHECK(geometry(*s.snap) == geometry(*f.snap));
    CHECK(showing(tree.root(), "name 5")->absolute_y == showing(fresh.root(), "name 5")->absolute_y);
}

TEST_CASE("a drop target among kept rows is still found") {
    struct Docked {
        std::array<Field<Label<>>, 10> names{};
        Field<Label<>> target{{"drop here"}};
        void view(WidgetTree::ViewBuilder& vb) {
            for (auto& n : names) vb.widget(n);
            DragRole takes_ints{.accepts = [](const std::any& v) { return v.type() == typeid(int); },
                                .drop = [](const std::any&, DropPlace) {}};
            vb.with_drag(takes_ints, [&] { vb.hstack(target); });
        }
    } m;
    WidgetTree tree{m};
    Settled s{tree};
    s.frame();
    auto* box = find_node(tree.root(), [](WidgetNode& n) { return n.is_container && bool(n.drag.accepts); });
    REQUIRE(box);
    CHECK(tree.drop_area_rect(box->id).has_value());
}

TEST_CASE("a text area's scrollbar fades out among kept rows") {
    struct Notes {
        std::array<Field<Label<>>, 10> names{};
        Field<TextArea<>> notes{{.value = "a\nb\nc"}};
        void view(WidgetTree::ViewBuilder& vb) {
            for (auto& n : names) vb.widget(n);
            vb.hstack(notes);
        }
    } m;
    WidgetTree tree{m};
    Settled s{tree};
    auto* area = find_node(tree.root(), [](WidgetNode& n) { return std::any_cast<TextAreaEditState>(&n.edit_state); });
    REQUIRE(area);
    auto& st = *std::any_cast<TextAreaEditState>(&area->edit_state);
    st.show_ticks = 5;
    s.frame();
    s.frame();
    CHECK(st.show_ticks == 3);
}

TEST_CASE("a column of small widgets is as wide as they say, not the 200 px they first draw at") {
    struct Strip {
        Field<Label<>> name{{"ch"}};
        Field<Checkbox> mute{{.label = "Mute"}};
        void view(WidgetTree::ViewBuilder& vb) { vb.vstack([&] { vb.widget(name); vb.widget(mute); }); }
    };
    struct Mixer {
        std::array<Strip, 8> strips{};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.hstack([&] {
                for (auto& s : strips) vb.block(s);
                vb.spacer();
            });
        }
    } m;
    for (std::size_t i = 0; i < m.strips.size(); ++i) m.strips[i].name.set(Label<>{"ch" + std::to_string(i)});
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(1000, 300, 1);
    snap = tree.build_snapshot(1000, 300, 2);
    auto last = rect_showing(*snap, "ch7");
    REQUIRE(last.extent.w.raw() > 0);
    CHECK(last.origin.x.raw() + 100.f < 1000.f);   // the eighth strip fits, with room to spare
}
