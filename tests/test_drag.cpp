// Dragging inside the app (doc/design/drag-and-docking.md, part 1): a source gives a typed value,
// a target accepts it by type.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <algorithm>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace drag_test {
struct Track {
    int id = 0;
};

struct Board {
    Field<Button> track{{"Song"}};             // a source: drags a Track
    Field<Label<>> playlist{{"Playlist"}};     // a target for Tracks
    Field<Label<>> bin{{"Bin"}};               // a target for strings only
    Field<TextField<>> note{{}};               // a text field: takes a dragged string as typing
    Field<Label<>> word{{"word"}};             // a source: drags a std::string
    Field<bool> playlist_on{true};
    std::vector<int> dropped;
    std::optional<DropPlace> place;
    int bin_drops = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(track).drag_source([] { return Track{7}; });
            vb.widget(playlist).enabled(playlist_on).drop_target([this](const Track& t, DropPlace at) {
                dropped.push_back(t.id);
                place = at;
            });
            vb.widget(bin).drop_target([this](const std::string&, DropPlace) { ++bin_drops; });
            vb.widget(note);
            vb.widget(word).drag_source([] { return std::string{"hello"}; });
        });
    }
};
}  // namespace drag_test

using namespace drag_test;

namespace {
struct Fixture {
    Board model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    std::vector<WidgetId> ids = tree.leaf_ids();   // track, playlist, bin, note, word
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    Point centre(WidgetId id) {
        auto r = find_widget_rect(*snap, id);
        REQUIRE(r);
        return Point{r->origin.x + DX{r->extent.w.raw() / 2}, r->origin.y + DY{r->extent.h.raw() / 2}};
    }
    void press(Point p) { route_event(tree, snap.get(), MouseButton{p, buttons::left, true}); }
    void move(Point p) { route_event(tree, snap.get(), MouseMove{p}); }
    void release(Point p) { route_event(tree, snap.get(), MouseButton{p, buttons::left, false}); }
    void drag(WidgetId from, WidgetId to) {
        auto a = centre(from), b = centre(to);
        press(a);
        move(Point{a.x + DX{10}, a.y});
        move(b);
        release(b);
    }
};
}

TEST_CASE("dragging a source onto a target that takes its type drops the value there") {
    Fixture f;
    f.drag(f.ids[0], f.ids[1]);
    CHECK(f.model.dropped == std::vector<int>{7});
    REQUIRE(f.model.place);
    auto r = find_widget_rect(*f.snap, f.ids[1]);
    CHECK(f.model.place->at.x.raw() == doctest::Approx(r->extent.w.raw() / 2));   // in the target's coordinates
    CHECK(f.model.place->size.h.raw() == doctest::Approx(r->extent.h.raw()));
    CHECK(f.model.track.get().click_count == 0);   // a drag is not a click
}

TEST_CASE("a press and release without moving far is a click, not a drag") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{2}, a.y});
    f.release(Point{a.x + DX{2}, a.y});
    CHECK(f.model.track.get().click_count == 1);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("a target of another type refuses the drop and shows the no-entry cursor") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[2]));   // the bin takes strings, not Tracks
    CHECK(f.tree.desired_cursor() == CursorShape::NotAllowed);
    f.move(f.centre(f.ids[1]));
    CHECK(f.tree.desired_cursor() == CursorShape::Default);
    f.move(f.centre(f.ids[2]));
    f.release(f.centre(f.ids[2]));
    CHECK(f.model.bin_drops == 0);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("Escape cancels a drag") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[1]));
    route_event(f.tree, f.snap.get(), KeyPress{keys::escape, 0});
    f.release(f.centre(f.ids[1]));
    CHECK(f.model.dropped.empty());
    CHECK(f.tree.desired_cursor() == CursorShape::Default);
}

TEST_CASE("a disabled target takes nothing") {
    Fixture f;
    f.model.playlist_on.set(false);
    f.frame();
    f.drag(f.ids[0], f.ids[1]);
    CHECK(f.model.dropped.empty());
}

TEST_CASE("a dragged string dropped on a text field is typed into it") {
    Fixture f;
    f.drag(f.ids[4], f.ids[3]);
    CHECK(f.model.note.get().value == "hello");
}

TEST_CASE("while dragging, the source follows the pointer, faded, and the target is outlined") {
    Fixture f;
    auto a = f.centre(f.ids[0]);
    f.press(a);
    f.move(Point{a.x + DX{10}, a.y});
    f.move(f.centre(f.ids[1]));
    f.frame();
    bool ghost = false;
    for (auto& c : f.snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "Song") ghost = t->color.a < 255;
    CHECK(ghost);
    auto target = find_widget_rect(*f.snap, f.ids[1]);
    bool outlined = false;
    for (auto& c : f.snap->overlay.commands)
        if (auto* o = std::get_if<RectOutline>(&c); o && o->rect.origin == target->origin) outlined = true;
    CHECK(outlined);
    f.release(f.centre(f.ids[1]));
    f.frame();
    for (auto& c : f.snap->overlay.commands)
        CHECK_FALSE((std::holds_alternative<TextCmd>(c) && std::get<TextCmd>(c).text == "Song"));
}

TEST_CASE("a click lands on release: sliding off the button before letting go cancels it") {
    Fixture f;
    auto ok = f.centre(f.ids[0]);
    f.press(ok);
    f.release(ok);
    CHECK(f.model.track.get().click_count == 1);
    f.press(ok);
    CHECK(f.model.track.get().click_count == 1);   // not yet: on release
    auto off = Point{X{ok.x.raw()}, Y{299}};       // well below the button (no target there)
    f.release(off);
    CHECK(f.model.track.get().click_count == 1);
}

namespace drag_test {
// A pen drags a Track drawn as a trail (a wire, say), not as a ghost of itself.
struct Trails {
    explicit Trails(bool hint) : hint(hint) {}
    Field<Label<>> pen{{"Pen"}};
    Field<Label<>> a{{"A"}};          // takes Tracks
    Field<Label<>> b{{"B"}};          // takes Tracks
    Field<Label<>> words{{"Words"}};  // takes strings only
    bool hint;
    std::vector<std::pair<int, Point>> trails;   // what the trail was drawn with
    void view(WidgetTree::ViewBuilder& vb) {
        DragRole role;
        role.source = [](const WidgetNode&, Point) { return std::any(Track{7}); };
        role.trail = [this](DrawList& dl, const std::any& v, Point pointer) {
            trails.emplace_back(std::any_cast<const Track&>(v).id, pointer);
            dl.circle(pointer, 3.f, Color::rgba(1, 2, 3));
        };
        role.hint_targets = hint;
        vb.vstack([&] {
            vb.with_drag(role, [&] { vb.widget(pen); });
            vb.widget(a).drop_target([](const Track&, DropPlace) {});
            vb.widget(b).drop_target([](const Track&, DropPlace) {});
            vb.widget(words).drop_target([](const std::string&, DropPlace) {});
        });
    }
};
}

namespace {
template <bool Hint>
struct TrailFixture {
    Trails model{Hint};
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    std::vector<WidgetId> ids = tree.leaf_ids();   // pen, a, b, words
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    Rect rect(WidgetId id) const {
        auto r = find_widget_rect(*snap, id);
        REQUIRE(r);
        return *r;
    }
    Point centre(WidgetId id) const {
        auto r = rect(id);
        return Point{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
    }
    // Starts dragging the pen and holds it over `id`.
    void drag_over(WidgetId id) {
        auto at = centre(ids[0]);
        route_event(tree, snap.get(), MouseButton{at, buttons::left, true});
        route_event(tree, snap.get(), MouseMove{Point{at.x + DX{10}, at.y}});
        route_event(tree, snap.get(), MouseMove{centre(id)});
        frame();
    }
    bool hinted(WidgetId id) const {
        return std::ranges::any_of(snap->overlay.commands, [&](const DrawCmd& c) {
            auto* f = std::get_if<FilledRect>(&c);
            return f && f->rect.origin == rect(id).origin && f->rect.extent == rect(id).extent;
        });
    }
};
}

TEST_CASE("a source with a trail draws it instead of a ghost") {
    TrailFixture<false> f;
    f.drag_over(f.ids[1]);
    REQUIRE_FALSE(f.model.trails.empty());
    CHECK(f.model.trails.back().first == 7);
    CHECK(f.model.trails.back().second == f.centre(f.ids[1]));
    CHECK(std::ranges::any_of(f.snap->overlay.commands, [&](const DrawCmd& c) {
        auto* d = std::get_if<Circle>(&c);
        return d && d->center == f.centre(f.ids[1]);
    }));
    CHECK_FALSE(std::ranges::any_of(f.snap->overlay.commands, [](const DrawCmd& c) {
        auto* t = std::get_if<TextCmd>(&c);
        return t && t->text == "Pen";
    }));
}

TEST_CASE("hint_targets marks every accepting target") {
    TrailFixture<true> f;
    f.drag_over(f.ids[3]);   // over a target that refuses it
    CHECK(f.hinted(f.ids[1]));
    CHECK(f.hinted(f.ids[2]));
    CHECK_FALSE(f.hinted(f.ids[3]));
    CHECK_FALSE(f.hinted(f.ids[0]));

    TrailFixture<false> plain;
    plain.drag_over(plain.ids[3]);
    CHECK_FALSE(plain.hinted(plain.ids[1]));
    CHECK_FALSE(plain.hinted(plain.ids[2]));
}

namespace drag_test {
struct Lists {
    List<std::string> todo;
    List<std::string> done;
    Lists() {
        for (auto s : {"a", "b", "c", "d"}) todo.push_back(s);
        done.push_back("x");
    }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.list(todo).reorderable();
            vb.list(done).reorderable();
        });
    }
};
}

namespace {
std::optional<Rect> text_rect(const SceneSnapshot& snap, std::string_view text) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i)
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return snap.geometry[i].second;
    return std::nullopt;
}
std::vector<std::string> items(const List<std::string>& l) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < l.size(); ++i) out.push_back(l[i]);
    return out;
}
}

TEST_CASE("a reorderable list moves a row to where it is dropped, and shows it there") {
    Lists model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto a = text_rect(*snap, "a"), c = text_rect(*snap, "c");
    REQUIRE(a);
    REQUIRE(c);
    Point from{a->origin.x + DX{5}, a->origin.y + DY{a->extent.h.raw() / 2}};
    Point to{c->origin.x + DX{5}, c->origin.y + DY{c->extent.h.raw() * 0.75f}};   // c's lower half: after c
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x, from.y + DY{6}}});
    route_event(tree, snap.get(), MouseMove{to});
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(items(model.todo) == std::vector<std::string>{"b", "c", "a", "d"});
    snap = tree.build_snapshot(400, 300, 2);
    auto a2 = text_rect(*snap, "a"), c2 = text_rect(*snap, "c");
    REQUIRE(a2);
    REQUIRE(c2);
    CHECK(a2->origin.y.raw() > c2->origin.y.raw());   // drawn below c now
}

TEST_CASE("a row is not dropped into another list") {
    Lists model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto a = text_rect(*snap, "a"), x = text_rect(*snap, "x");
    Point from{a->origin.x + DX{5}, a->origin.y + DY{5}};
    Point to{x->origin.x + DX{5}, x->origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x, from.y + DY{6}}});
    route_event(tree, snap.get(), MouseMove{to});
    CHECK(tree.desired_cursor() == CursorShape::NotAllowed);
    route_event(tree, snap.get(), MouseButton{to, buttons::left, false});
    CHECK(items(model.todo) == std::vector<std::string>{"a", "b", "c", "d"});
    CHECK(items(model.done) == std::vector<std::string>{"x"});
}

TEST_CASE("over a list row, an insertion line shows where the row will go") {
    Lists model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto a = text_rect(*snap, "a"), c = text_rect(*snap, "c");
    Point from{a->origin.x + DX{5}, a->origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{from, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{from.x, from.y + DY{6}}});
    auto line_at = [&](float y) {
        snap = tree.build_snapshot(400, 300, 2);
        for (auto& cmd : snap->overlay.commands)
            if (auto* l = std::get_if<Line>(&cmd); l && std::abs(l->from.y.raw() - y) < 2.f) return true;
        return false;
    };
    float c_bottom = c->origin.y.raw() + c->extent.h.raw();
    route_event(tree, snap.get(), MouseMove{Point{c->origin.x + DX{5}, c->origin.y + DY{c->extent.h.raw() * 0.8f}}});
    CHECK(line_at(c_bottom));
    route_event(tree, snap.get(), MouseMove{Point{c->origin.x + DX{5}, c->origin.y + DY{c->extent.h.raw() * 0.2f}}});
    CHECK(line_at(c->origin.y.raw()));
}

// --- moving tree nodes -----------------------------------------------------------------

namespace drag_test {
// root(1) -> {2, 3}; 2 -> {4}
struct SmallTree {
    size_t root_count() const { return 1; }
    TreeNodeId root_at(size_t) const { return 1; }
    size_t child_count(TreeNodeId id) const { return id == 1 ? 2 : id == 2 ? 1 : 0; }
    TreeNodeId child_at(TreeNodeId id, size_t i) const { return id == 1 ? (i == 0 ? 2 : 3) : 4; }
    std::string label(TreeNodeId id) const { return "n" + std::to_string(id); }
    bool has_children(TreeNodeId id) const { return id == 1 || id == 2; }
    std::vector<std::pair<std::string, std::string>> attributes(TreeNodeId) const { return {}; }
};

struct Explorer {
    SmallTree data;
    TreeController files{wrap_tree_storage(data)};
    struct Move { TreeNodeId node, target; TreeDrop where; };
    std::vector<Move> moves;
    Explorer() {
        files.on_move = [this](TreeNodeId n, TreeNodeId t, TreeDrop w) { moves.push_back({n, t, w}); };
        files.on_row_clicked(0, files.rows[0]);   // expand the root
        files.on_row_clicked(1, files.rows[1]);   // expand n2: n1, n2, n4, n3
    }
    void view(WidgetTree::ViewBuilder& vb) { vb.tree(files, /*detail=*/false); }
};
}

namespace {
void drag_between(WidgetTree& tree, std::unique_ptr<SceneSnapshot>& snap, std::string_view from, std::string_view to, float frac) {
    auto a = text_rect(*snap, from), b = text_rect(*snap, to);
    REQUIRE(a);
    REQUIRE(b);
    Point p{a->origin.x + DX{5}, a->origin.y + DY{a->extent.h.raw() / 2}};
    Point q{b->origin.x + DX{5}, b->origin.y + DY{b->extent.h.raw() * frac}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{p.x, p.y + DY{6}}});
    route_event(tree, snap.get(), MouseMove{q});
    route_event(tree, snap.get(), MouseButton{q, buttons::left, false});
}
}

TEST_CASE("a tree node dropped on another's middle goes inside it; near an edge, before or after") {
    Explorer model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    drag_between(tree, snap, "n3", "n2", 0.5f);
    drag_between(tree, snap, "n4", "n3", 0.9f);
    drag_between(tree, snap, "n3", "n2", 0.1f);
    REQUIRE(model.moves.size() == 3);
    CHECK((model.moves[0].node == 3 && model.moves[0].target == 2 && model.moves[0].where == TreeDrop::Inside));
    CHECK((model.moves[1].node == 4 && model.moves[1].target == 3 && model.moves[1].where == TreeDrop::After));
    CHECK((model.moves[2].node == 3 && model.moves[2].target == 2 && model.moves[2].where == TreeDrop::Before));
}

TEST_CASE("a tree node is never dropped on itself or inside its own subtree") {
    Explorer model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    drag_between(tree, snap, "n2", "n4", 0.5f);   // n4 is n2's child
    drag_between(tree, snap, "n1", "n3", 0.5f);   // everything is under n1
    CHECK(model.moves.empty());
}

TEST_CASE("a tree without on_move cannot be dragged") {
    SmallTree data;
    struct Plain {
        TreeController& files;
        void view(WidgetTree::ViewBuilder& vb) { vb.tree(files, false); }
    };
    TreeController files{wrap_tree_storage(data)};
    files.on_row_clicked(0, files.rows[0]);
    Plain model{files};
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(400, 300, 1);
    auto a = text_rect(*snap, "n2");
    Point p{a->origin.x + DX{5}, a->origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
    route_event(tree, snap.get(), MouseMove{Point{p.x, p.y + DY{30}}});
    CHECK_FALSE(tree.dragging());
    route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
}
