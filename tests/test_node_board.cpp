// vb.node_board: a graph shown as cards on a board, its wires drawn from card edge to card edge.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/graph.hpp>

#include <algorithm>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;
namespace nodes = prism::nodes;

namespace {

Point P(float x, float y) { return {X{x}, Y{y}}; }

struct Source {
    nodes::Out<double> out{"out", [] { return 1.0; }};
    WidgetId out_label = 0;
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(WidgetTree::ViewBuilder& vb) { out_label = vb.port(out).node_ref.id; }
};

struct Sink {
    nodes::In<double> in{"in", 0.0};
    Field<Label<>> note{{"gain"}};
    WidgetId in_label = 0;
    std::vector<nodes::Port*> ports() { return {&in}; }
    void view(WidgetTree::ViewBuilder& vb) {
        in_label = vb.port(in).node_ref.id;
        vb.widget(note);
    }
};

struct Editor {
    Source source;
    Sink sink;
    nodes::Graph graph;   // after the nodes: removed first
    nodes::NodeId a = graph.add(source, {.title = "Source", .family = "source", .at = P(20, 30)});
    nodes::NodeId b = graph.add(sink, {.title = "Sink", .family = "math", .at = P(300, 60), .removable = true});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

// The board away from the window's origin: a bar above it, a button to its left, one below.
struct Framed : Editor {
    Field<Label<>> bar{{"toolbar"}};
    Field<Button> side{{"side"}};
    Field<Button> after{{"after"}};
    WidgetId board = 0, side_id = 0, after_id = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.widget(bar);
        vb.hstack([&] {
            side_id = vb.widget(side).node_ref.id;
            board = vb.node_board(graph);
        });
        after_id = vb.widget(after).node_ref.id;
    }
};

template <class Model>
struct BasicFixture {
    Model m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(800, 600, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(800, 600, ++v); }

    template <typename Cmd>
    std::vector<Cmd> all() const {
        std::vector<Cmd> out;
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* x = std::get_if<Cmd>(&c)) out.push_back(*x);
        return out;
    }
    std::optional<Point> text_at(std::string_view text) const {
        for (auto& t : all<TextCmd>()) if (t.text == text) return t.origin;
        return std::nullopt;
    }
    bool shows(std::string_view text) const { return text_at(text).has_value(); }
    // The rect a widget was laid out in: its draws are clipped to it.
    std::optional<Rect> allocated(WidgetId id) const {
        for (std::size_t i = 0; i < snap->geometry.size(); ++i)
            if (snap->geometry[i].first == id && !snap->draw_lists[i]->commands.empty())
                if (auto* c = std::get_if<ClipPush>(&snap->draw_lists[i]->commands.front())) return c->rect;
        return std::nullopt;
    }
    void press(Point at) { route_event(tree, snap.get(), MouseButton{at, buttons::left, true}); frame(); }
    void move_to(Point at) { route_event(tree, snap.get(), MouseMove{at, 1}); frame(); }
    void release(Point at) { route_event(tree, snap.get(), MouseButton{at, buttons::left, false}); frame(); }
    void drag(Point from, Point to) { press(from); move_to(to); release(to); }
    WidgetId card_of(WidgetId label) const { return *tree.parent_of(*tree.parent_of(label)); }
    // A port's row draws nothing, so it has no entry of its own: its rect is the union of what
    // its widgets were given (a row gives each its full height).
    std::optional<Rect> row_of(WidgetId label) const {
        const WidgetId row = *tree.parent_of(label);
        std::optional<Rect> out;
        for (auto& [id, r] : snap->geometry) {
            if (id == 0 || tree.parent_of(id) != std::optional{row}) continue;
            auto a = allocated(id);
            if (!a) continue;
            if (!out) { out = a; continue; }
            float x0 = std::min(out->origin.x.raw(), a->origin.x.raw());
            float y0 = std::min(out->origin.y.raw(), a->origin.y.raw());
            float x1 = std::max(out->origin.x.raw() + out->extent.w.raw(), a->origin.x.raw() + a->extent.w.raw());
            float y1 = std::max(out->origin.y.raw() + out->extent.h.raw(), a->origin.y.raw() + a->extent.h.raw());
            out = Rect{Point{X{x0}, Y{y0}}, Size{Width{x1 - x0}, Height{y1 - y0}}};
        }
        return out;
    }
};

using Fixture = BasicFixture<Editor>;
using FramedFixture = BasicFixture<Framed>;

float right(Rect r) { return r.origin.x.raw() + r.extent.w.raw(); }
float middle_y(Rect r) { return r.origin.y.raw() + r.extent.h.raw() / 2.f; }

}

TEST_CASE("each node is a card with its title and its widgets") {
    Fixture f;
    CHECK(f.shows("Source"));
    CHECK(f.shows("source"));
    CHECK(f.shows("out"));
    CHECK(f.shows("Sink"));
    CHECK(f.shows("in"));
    CHECK(f.shows("gain"));
    CHECK(f.card_of(f.m.source.out_label) != f.card_of(f.m.sink.in_label));
}

TEST_CASE("a wire is drawn from the output's card edge to the input's") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    auto out_card = f.allocated(f.card_of(f.m.source.out_label));
    auto in_card = f.allocated(f.card_of(f.m.sink.in_label));
    auto out_row = f.row_of(f.m.source.out_label);
    auto in_row = f.row_of(f.m.sink.in_label);
    REQUIRE((out_card && in_card && out_row && in_row));

    auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].color == f.tree.theme().accent);
    auto first = wires[0].points.front(), last = wires[0].points.back();
    CHECK(first.x.raw() == doctest::Approx(right(*out_card)));
    CHECK(first.y.raw() == doctest::Approx(middle_y(*out_row)));
    CHECK(last.x.raw() == doctest::Approx(in_card->origin.x.raw()));
    CHECK(last.y.raw() == doctest::Approx(middle_y(*in_row)));

    auto dots = f.all<Circle>();
    CHECK(std::ranges::count_if(dots, [](const Circle& c) { return c.radius == 5.f; }) == 2);
    CHECK(std::ranges::any_of(dots, [&](const Circle& c) {
        return c.center.x.raw() == doctest::Approx(first.x.raw()) && c.center.y.raw() == doctest::Approx(first.y.raw());
    }));
}

TEST_CASE("moving a node relays it out without rebuilding its widgets") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    auto title = f.text_at("Source");
    auto card = f.card_of(f.m.source.out_label);
    auto before = f.allocated(card);
    REQUIRE((title && before));

    f.m.graph.move(f.m.a, P(50, 50));
    f.frame();

    CHECK(f.tree.leaf_ids() == ids);
    auto moved = f.text_at("Source");
    auto after = f.allocated(card);
    REQUIRE((moved && after));
    CHECK(moved->x.raw() == doctest::Approx(title->x.raw() + 30));
    CHECK(moved->y.raw() == doctest::Approx(title->y.raw() + 20));
    CHECK(after->origin.x.raw() == doctest::Approx(before->origin.x.raw() + 30));
    CHECK(after->origin.y.raw() == doctest::Approx(before->origin.y.raw() + 20));
}

TEST_CASE("connecting rebuilds nothing but adds a wire") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    CHECK(f.all<Polyline>().empty());
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    CHECK(f.tree.leaf_ids() == ids);
    CHECK(f.all<Polyline>().size() == 1);
}

TEST_CASE("a node added later gets its card") {
    Fixture f;
    Source extra;
    f.m.graph.add(extra, {.title = "Extra", .family = "source", .at = P(20, 200)});
    f.frame();
    CHECK(f.shows("Extra"));
    f.m.graph.remove(f.m.graph.nodes().back());
    f.frame();
    CHECK_FALSE(f.shows("Extra"));
}

TEST_CASE("moving a node or adding a wire asks for a frame") {
    Fixture f;
    f.tree.clear_dirty();
    REQUIRE_FALSE(f.tree.any_dirty());
    f.m.graph.move(f.m.a, P(50, 50));
    CHECK(f.tree.any_dirty());
    f.frame();
    f.tree.clear_dirty();
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    CHECK(f.tree.any_dirty());
}

namespace {
struct Add {   // no view: the board places its ports
    nodes::In<double> x{"x", 0.0};
    nodes::In<double> y{"y", 0.0};
    nodes::Out<double> sum{"sum", [this] { return x.get() + y.get(); }};
    std::vector<nodes::Port*> ports() { return {&sum, &x, &y}; }
};
}

TEST_CASE("a node without a view shows its ports and its wires") {
    Fixture f;
    Add add;
    auto c = f.m.graph.add(add, {.title = "Add", .family = "math", .at = P(200, 300)});
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {c, 1}).has_value());
    REQUIRE(f.m.graph.connect({c, 0}, {f.m.b, 0}).has_value());
    f.frame();
    auto x = f.text_at("x"), y = f.text_at("y"), sum = f.text_at("sum");
    REQUIRE((x && y && sum));
    CHECK(x->y.raw() < y->y.raw());     // inputs first, in ports() order
    CHECK(y->y.raw() < sum->y.raw());   // then the outputs
    CHECK(f.all<Polyline>().size() == 2);
    f.m.graph.remove(c);
    f.frame();
}

namespace {
constexpr Point empty_spot{X{700}, Y{500}};   // on the board, under no card
}

TEST_CASE("dragging a header by (30, 20) moves its node by (30, 20)") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    auto title = f.text_at("Source");
    REQUIRE(title);
    const Point grab = *title + Offset{DX{4.f}, DY{4.f}};
    f.drag(grab, grab + Offset{DX{30.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a) == P(50, 50));
    CHECK(f.m.graph.position(f.m.b) == P(300, 60));
    auto moved = f.text_at("Source");
    REQUIRE(moved);
    CHECK(moved->x.raw() == doctest::Approx(title->x.raw() + 30));
    CHECK(moved->y.raw() == doctest::Approx(title->y.raw() + 20));
    CHECK(f.tree.leaf_ids() == ids);
}

TEST_CASE("the header's family label takes the drag too, and a second drag goes on from there") {
    Fixture f;
    auto family = f.text_at("source");
    REQUIRE(family);
    const Point grab = *family + Offset{DX{4.f}, DY{4.f}};
    f.drag(grab, grab + Offset{DX{10.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a) == P(30, 30));
    const Point on_family = *f.text_at("source") + Offset{DX{4.f}, DY{4.f}};
    f.drag(on_family, on_family + Offset{DX{0.f}, DY{10.f}});
    CHECK(f.m.graph.position(f.m.a) == P(30, 40));
}

TEST_CASE("moves read against a frame that shows the node where it was do not overshoot") {
    Fixture f;
    const Point grab = *f.text_at("Source") + Offset{DX{4.f}, DY{4.f}};
    f.press(grab);
    route_event(f.tree, f.snap.get(), MouseMove{grab + Offset{DX{10.f}, DY{0.f}}, 1});
    route_event(f.tree, f.snap.get(), MouseMove{grab + Offset{DX{30.f}, DY{20.f}}, 1});   // same frame
    f.frame();
    CHECK(f.m.graph.position(f.m.a) == P(50, 50));
    f.move_to(grab + Offset{DX{40.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a) == P(60, 50));
    f.release(grab + Offset{DX{40.f}, DY{20.f}});
}

TEST_CASE("a drag starting on a widget in a node's body does not move the node") {
    Fixture f;
    const Point on_port = *f.text_at("out") + Offset{DX{4.f}, DY{4.f}};
    f.drag(on_port, on_port + Offset{DX{30.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a) == P(20, 30));
}

TEST_CASE("dragging the background pans all nodes") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    auto source = f.text_at("Source"), sink = f.text_at("Sink");
    REQUIRE((source && sink));
    f.drag(empty_spot, empty_spot + Offset{DX{-50.f}, DY{-40.f}});
    CHECK(f.m.graph.view().pan == P(-50, -40));
    CHECK(f.m.graph.position(f.m.a) == P(20, 30));   // the nodes stay where they are in the graph
    auto s = f.text_at("Source"), k = f.text_at("Sink");
    REQUIRE((s && k));
    CHECK(s->x.raw() == doctest::Approx(source->x.raw() - 50));
    CHECK(s->y.raw() == doctest::Approx(source->y.raw() - 40));
    CHECK(k->x.raw() == doctest::Approx(sink->x.raw() - 50));
    CHECK(k->y.raw() == doctest::Approx(sink->y.raw() - 40));
    CHECK(f.tree.leaf_ids() == ids);
}

TEST_CASE("a node panned off the board and back is where it was") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    auto card = f.card_of(f.m.source.out_label);
    auto before = f.allocated(card);
    REQUIRE(before);
    f.drag(empty_spot, empty_spot + Offset{DX{-1500.f}, DY{0.f}});
    CHECK_FALSE(f.shows("Source"));
    CHECK_FALSE(f.shows("Sink"));
    f.drag(P(400, 300), P(1900, 300));
    CHECK(f.m.graph.view().pan == P(0, 0));
    auto after = f.allocated(card);
    REQUIRE(after);
    CHECK(after->origin == before->origin);
    CHECK(after->extent == before->extent);
    CHECK(f.tree.leaf_ids() == ids);
}

TEST_CASE("Tab never stops on a header's grip or on the background") {
    Fixture f;   // labels only: nothing on the board takes the focus
    f.tree.focus_next();
    CHECK(f.tree.focused_id() == 0);
}

TEST_CASE("a right click in the middle of a drag does not make the node jump") {
    Fixture f;
    const Point grab = *f.text_at("Source") + Offset{DX{4.f}, DY{4.f}};
    f.press(grab);
    f.move_to(grab + Offset{DX{10.f}, DY{0.f}});
    const Point here = grab + Offset{DX{10.f}, DY{0.f}};
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::right, true});
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::right, false});
    f.frame();
    f.move_to(grab + Offset{DX{30.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a) == P(50, 50));
    f.release(grab + Offset{DX{30.f}, DY{20.f}});
}

TEST_CASE("a right click in the middle of a pan does not make the board jump") {
    Fixture f;
    f.press(empty_spot);
    f.move_to(empty_spot + Offset{DX{-10.f}, DY{0.f}});
    const Point here = empty_spot + Offset{DX{-10.f}, DY{0.f}};
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::right, true});
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::right, false});
    f.frame();
    f.move_to(empty_spot + Offset{DX{-30.f}, DY{-20.f}});
    CHECK(f.m.graph.view().pan == P(-30, -20));
    f.release(empty_spot + Offset{DX{-30.f}, DY{-20.f}});
}

TEST_CASE("a node off the board is still wired") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.m.graph.pan_by(DX{-250}, DY{0});   // Source wholly off on the left, Sink still on
    f.frame();
    CHECK_FALSE(f.shows("Source"));
    CHECK(f.shows("Sink"));
    auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].points.front().x.raw() < 0.f);
}

TEST_CASE("a node can sit at a negative position") {
    Fixture f;
    auto before = f.text_at("Source");   // text, not the card's rect: that is cut to the board
    REQUIRE(before);
    f.m.graph.move(f.m.a, P(-40, -40));
    f.frame();
    CHECK(f.m.graph.position(f.m.a) == P(-40, -40));
    auto after = f.text_at("Source");
    REQUIRE(after);
    CHECK(after->x.raw() == doctest::Approx(before->x.raw() - 60));
    CHECK(after->y.raw() == doctest::Approx(before->y.raw() - 70));
}

namespace {
struct Counter {   // an input of another type than double
    nodes::In<int> count{"count", 0};
    WidgetId count_label = 0;
    std::vector<nodes::Port*> ports() { return {&count}; }
    void view(WidgetTree::ViewBuilder& vb) { count_label = vb.port(count).node_ref.id; }
};

template <class F>
Point centre(const F& f, WidgetId id) {
    auto r = find_widget_rect(*f.snap, id);
    REQUIRE(r);
    return Point{r->origin.x + DX{r->extent.w.raw() / 2}, r->origin.y + DY{r->extent.h.raw() / 2}};
}

template <class F>
void key(F& f, std::int32_t k) {
    route_event(f.tree, f.snap.get(), KeyPress{k, 0});
    f.frame();
}

// A port's whole row, its name and the space beside it.
template <class F>
bool hinted(const F& f, WidgetId label) {
    auto r = f.tree.laid_out_at(*f.tree.parent_of(label));
    return r && std::ranges::any_of(f.snap->overlay.commands, [&](const DrawCmd& c) {
        auto* x = std::get_if<FilledRect>(&c);
        return x && x->rect.origin == r->origin && x->rect.extent == r->extent;
    });
}
}

TEST_CASE("dragging from an output onto a compatible input connects them") {
    Fixture f;
    auto ids = f.tree.leaf_ids();
    const Point from = centre(f, f.m.source.out_label), to = centre(f, f.m.sink.in_label);
    f.press(from);
    const Point half_way = P((from.x.raw() + to.x.raw()) / 2, (from.y.raw() + to.y.raw()) / 2);
    f.move_to(half_way);

    // A wire follows the pointer from the output's card edge; the input it may join is hinted.
    auto out_card = f.allocated(f.card_of(f.m.source.out_label));
    auto out_row = f.row_of(f.m.source.out_label);
    REQUIRE((out_card && out_row));
    std::vector<Polyline> trails;
    for (auto& c : f.snap->overlay.commands)
        if (auto* p = std::get_if<Polyline>(&c)) trails.push_back(*p);
    REQUIRE(trails.size() == 1);
    CHECK(trails[0].color == f.tree.theme().accent);
    CHECK(trails[0].points.front().x.raw() == doctest::Approx(right(*out_card)));
    CHECK(trails[0].points.front().y.raw() == doctest::Approx(middle_y(*out_row)));
    CHECK(trails[0].points.back() == half_way);
    CHECK(hinted(f, f.m.sink.in_label));
    CHECK_FALSE(hinted(f, f.m.source.out_label));

    f.move_to(to);
    f.release(to);
    auto e = f.m.graph.edge_into({f.m.b, 0});
    REQUIRE(e);
    CHECK(f.m.graph.edge(*e)->from == nodes::PortRef{f.m.a, 0});
    CHECK(f.tree.leaf_ids() == ids);
}

TEST_CASE("a drop on an incompatible input, on an output, or on the starting port changes nothing") {
    Fixture f;
    Counter counter;
    Source other;
    f.m.graph.add(counter, {.title = "Counter", .family = "math", .at = P(300, 300)});
    f.m.graph.add(other, {.title = "Other", .family = "source", .at = P(20, 300)});
    f.frame();
    const Point from = centre(f, f.m.source.out_label);
    f.drag(from, centre(f, counter.count_label));   // an int input
    CHECK(f.m.graph.edges().empty());
    f.drag(from, centre(f, other.out_label));       // another output
    CHECK(f.m.graph.edges().empty());
    f.press(from);                                    // away and back to where it started
    f.move_to(from + Offset{DX{30.f}, DY{0.f}});
    f.move_to(from);
    f.release(from);
    CHECK(f.m.graph.edges().empty());
    f.m.graph.remove(f.m.graph.nodes().back());
    f.m.graph.remove(f.m.graph.nodes().back());
}

TEST_CASE("dragging a wire off its input and letting go on the board removes it") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    const Point in = centre(f, f.m.sink.in_label);
    f.press(in);
    f.move_to(in + Offset{DX{-30.f}, DY{0.f}});
    CHECK(f.m.graph.edges().empty());   // picked up when the drag starts
    f.move_to(in);
    f.release(in);
    REQUIRE(f.m.graph.edge_into({f.m.b, 0}));   // dropped back on its input
    f.drag(in, empty_spot);
    CHECK(f.m.graph.edges().empty());
    CHECK(f.m.sink.in.get() == 0.0);   // back to its default
}

TEST_CASE("click a wire, press Delete: it is gone") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].thickness == 2.f);
    const Point on_wire = wires[0].points[12] + Offset{DX{0.f}, DY{4.f}};
    f.drag(on_wire, on_wire + Offset{DX{20.f}, DY{0.f}});
    CHECK(f.m.graph.view().pan == P(0, 0));   // a press on a wire selects it, it does not pan
    wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].thickness == 4.f);    // selected: drawn thicker
    key(f, keys::delete_);
    CHECK(f.m.graph.edges().empty());

    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    f.press(empty_spot);   // the background: nothing selected any more
    f.release(empty_spot);
    key(f, keys::backspace);
    CHECK(f.m.graph.edges().size() == 1);
}

TEST_CASE("a node that is not removable stays on Delete") {
    Fixture f;
    Source fixed;
    auto id = f.m.graph.add(fixed, {.title = "Fixed", .family = "source", .at = P(20, 300), .removable = false});
    f.frame();
    const Point header = *f.text_at("Fixed") + Offset{DX{4.f}, DY{4.f}};
    f.press(header);
    f.release(header);
    const auto outlined = [&](WidgetId card) {
        auto r = f.allocated(card);
        return r && std::ranges::any_of(f.all<RectOutline>(), [&](const RectOutline& o) {
            return o.color == f.tree.theme().accent && o.rect.origin == r->origin;
        });
    };
    CHECK(outlined(f.card_of(fixed.out_label)));   // a header click selects its node
    key(f, keys::delete_);
    CHECK(std::ranges::count(f.m.graph.nodes(), id) == 1);
    f.m.graph.move(f.m.a, P(30, 30));   // the board is drawn again: the node is still selected
    f.frame();
    CHECK(outlined(f.card_of(fixed.out_label)));

    const Point sink = *f.text_at("Sink") + Offset{DX{4.f}, DY{4.f}};
    f.press(sink);
    f.release(sink);
    CHECK_FALSE(outlined(f.card_of(fixed.out_label)));
    key(f, keys::backspace);
    CHECK(std::ranges::count(f.m.graph.nodes(), f.m.b) == 0);
    CHECK_FALSE(f.shows("Sink"));
    f.m.graph.remove(id);
}

TEST_CASE("a port's whole row takes a wire: the empty part beside its name too") {
    Fixture f;
    auto out = find_widget_rect(*f.snap, f.m.source.out_label);
    auto in = find_widget_rect(*f.snap, f.m.sink.in_label);
    REQUIRE((out && in));
    f.drag(P(out->origin.x.raw() - 10, middle_y(*out)), P(right(*in) + 10, middle_y(*in)));
    auto e = f.m.graph.edge_into({f.m.b, 0});
    REQUIRE(e);
    CHECK(f.m.graph.edge(*e)->from == nodes::PortRef{f.m.a, 0});
}

TEST_CASE("a wire starts and ends on the ports' dots, at the card's edge") {
    Fixture f;
    auto out_card = f.allocated(f.card_of(f.m.source.out_label));
    auto in_card = f.allocated(f.card_of(f.m.sink.in_label));
    auto out_row = f.row_of(f.m.source.out_label);
    auto in_row = f.row_of(f.m.sink.in_label);
    REQUIRE((out_card && in_card && out_row && in_row));
    const Point out_dot = P(right(*out_card), middle_y(*out_row));
    const Point in_dot = P(in_card->origin.x.raw(), middle_y(*in_row));

    f.press(out_dot + Offset{DX{4.f}, DY{0.f}});   // off the card: on the background
    f.move_to(empty_spot);
    // The board is no target as a whole: only the dots it takes the wire at.
    CHECK_FALSE(std::ranges::any_of(f.snap->overlay.commands, [](const DrawCmd& c) {
        auto* x = std::get_if<FilledRect>(&c);
        return x && x->rect.contains(empty_spot);
    }));
    f.move_to(in_dot + Offset{DX{-6.f}, DY{3.f}});   // near the dot is enough
    f.release(in_dot + Offset{DX{-6.f}, DY{3.f}});
    CHECK(f.m.graph.view().pan == P(0, 0));   // a press on a dot draws a wire, it does not pan
    REQUIRE(f.m.graph.edge_into({f.m.b, 0}));

    f.drag(in_dot, empty_spot);   // the input's dot picks its wire up
    CHECK(f.m.graph.edges().empty());
    CHECK(f.m.graph.view().pan == P(0, 0));
}

TEST_CASE("a node is not removable unless its options say so") {
    Fixture f;
    Source plain;
    auto id = f.m.graph.add(plain, {.title = "Plain", .family = "source", .at = P(20, 300)});
    f.frame();
    const Point header = *f.text_at("Plain") + Offset{DX{4.f}, DY{4.f}};
    f.press(header);
    f.release(header);
    key(f, keys::delete_);
    CHECK(std::ranges::count(f.m.graph.nodes(), id) == 1);
    f.m.graph.remove(id);
}

namespace {
Rect board_of(const FramedFixture& f) {
    auto r = f.tree.laid_out_at(f.m.board);
    REQUIRE(r);
    return *r;
}
}

TEST_CASE("a board away from the window's origin: wires, pans, moves and drops land where they should") {
    FramedFixture f;
    const Rect board = board_of(f);
    REQUIRE(board.origin.x.raw() > 0.f);
    REQUIRE(board.origin.y.raw() > 0.f);
    const Point on_board_empty = P(right(board) - 50, board.origin.y.raw() + board.extent.h.raw() - 30);

    f.drag(centre(f, f.m.source.out_label), centre(f, f.m.sink.in_label));
    REQUIRE(f.m.graph.edge_into({f.m.b, 0}));
    auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    auto out_card = f.allocated(f.card_of(f.m.source.out_label));
    REQUIRE(out_card);
    CHECK(wires[0].points.front().x.raw() == doctest::Approx(right(*out_card)));
    CHECK(out_card->origin.x.raw() == doctest::Approx(board.origin.x.raw() + 20));   // the node's place on the board

    auto title = f.text_at("Source");
    REQUIRE(title);
    f.drag(on_board_empty, on_board_empty + Offset{DX{-15.f}, DY{-10.f}});   // the title still on the board
    CHECK(f.m.graph.view().pan == P(-15, -10));
    auto panned = f.text_at("Source");
    REQUIRE(panned);
    CHECK(panned->x.raw() == doctest::Approx(title->x.raw() - 15));
    CHECK(panned->y.raw() == doctest::Approx(title->y.raw() - 10));

    const Point grab = *panned + Offset{DX{4.f}, DY{4.f}};
    f.drag(grab, grab + Offset{DX{30.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(50));   // window to widget: float rounding
    CHECK(f.m.graph.position(f.m.a).y.raw() == doctest::Approx(50));

    f.m.graph.disconnect(*f.m.graph.edge_into({f.m.b, 0}));
    f.frame();
    out_card = f.allocated(f.card_of(f.m.source.out_label));
    auto in_card = f.allocated(f.card_of(f.m.sink.in_label));
    auto out_row = f.row_of(f.m.source.out_label);
    auto in_row = f.row_of(f.m.sink.in_label);
    REQUIRE((out_card && in_card && out_row && in_row));
    const Point out_dot = P(right(*out_card), middle_y(*out_row));
    const Point in_dot = P(in_card->origin.x.raw(), middle_y(*in_row));
    f.press(out_dot + Offset{DX{4.f}, DY{0.f}});
    f.move_to(in_dot + Offset{DX{-6.f}, DY{3.f}});
    // The bar marking the port taking the drop is on the input's card edge.
    CHECK(std::ranges::any_of(f.snap->overlay.commands, [&](const DrawCmd& c) {
        auto* x = std::get_if<FilledRect>(&c);
        return x && x->rect.extent.w.raw() == doctest::Approx(3.f)
            && x->rect.origin.x.raw() == doctest::Approx(in_dot.x.raw() - 1.5f)
            && x->rect.origin.y.raw() == doctest::Approx(in_row->origin.y.raw());
    }));
    f.release(in_dot + Offset{DX{-6.f}, DY{3.f}});
    CHECK(f.m.graph.view().pan == P(-15, -10));
    REQUIRE(f.m.graph.edge_into({f.m.b, 0}));
}

TEST_CASE("a card overhanging a widget beside the board takes neither its clicks nor a dropped wire") {
    FramedFixture f;
    const Rect board = board_of(f);
    auto side = find_widget_rect(*f.snap, f.m.side_id);
    REQUIRE(side);
    f.m.graph.move(f.m.b, P(-40, 0));   // the Sink's left part lies over the button
    f.frame();
    auto sink_card = f.tree.laid_out_at(f.card_of(f.m.sink.in_label));   // not cut to the board
    REQUIRE(sink_card);
    REQUIRE(sink_card->origin.x.raw() < board.origin.x.raw());

    const Point on_side = P(board.origin.x.raw() - 10, middle_y(*side));
    REQUIRE(sink_card->contains(on_side));
    REQUIRE(side->contains(on_side));
    CHECK(hit_test(*f.snap, on_side) == std::optional{f.m.side_id});
    f.press(on_side);
    f.release(on_side);
    CHECK(f.m.side.get().click_count == 1);

    auto in_row = f.tree.laid_out_at(*f.tree.parent_of(f.m.sink.in_label));
    REQUIRE(in_row);
    const Point off_board_row = P(board.origin.x.raw() - 10, middle_y(*in_row));
    REQUIRE(in_row->contains(off_board_row));
    const Point from = centre(f, f.m.source.out_label);
    f.press(from);
    f.move_to(off_board_row);
    // No hint outside the board: the input's row is hinted only where it shows.
    CHECK_FALSE(std::ranges::any_of(f.snap->overlay.commands, [&](const DrawCmd& c) {
        auto* x = std::get_if<FilledRect>(&c);
        return x && x->rect.origin.x.raw() < board.origin.x.raw();
    }));
    f.release(off_board_row);
    CHECK_FALSE(f.m.graph.edge_into({f.m.b, 0}));
}

TEST_CASE("Tab from the focused board goes to the widget after it, Shift+Tab to the one before") {
    FramedFixture f;
    const Rect board = board_of(f);
    const Point empty = P(right(board) - 50, board.origin.y.raw() + board.extent.h.raw() - 30);
    f.press(empty);
    f.release(empty);
    REQUIRE(f.tree.focused_id() != 0);
    f.tree.focus_next();
    CHECK(f.tree.focused_id() == f.m.after_id);
    f.press(empty);
    f.release(empty);
    f.tree.focus_prev();
    CHECK(f.tree.focused_id() == f.m.side_id);
}

TEST_CASE("input reaching a node's widgets after the node was removed does nothing") {
    Fixture f;
    const Point header = *f.text_at("Source") + Offset{DX{4.f}, DY{4.f}};
    const Point in = centre(f, f.m.sink.in_label);
    f.m.graph.remove(f.m.a);   // no frame yet: the snapshot still shows the node
    route_event(f.tree, f.snap.get(), MouseButton{header, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseMove{header + Offset{DX{30.f}, DY{0.f}}, 1});
    route_event(f.tree, f.snap.get(), MouseButton{header + Offset{DX{30.f}, DY{0.f}}, buttons::left, false});
    f.m.graph.remove(f.m.b);
    route_event(f.tree, f.snap.get(), MouseButton{in, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseMove{in + Offset{DX{-30.f}, DY{0.f}}, 1});
    route_event(f.tree, f.snap.get(), MouseButton{in + Offset{DX{-30.f}, DY{0.f}}, buttons::left, false});
    f.frame();
    CHECK(f.m.graph.nodes().empty());
    CHECK_FALSE(f.shows("Source"));
}

namespace {
struct Level {   // a node with a widget and no port
    Field<Slider<>> level{{.value = 0.25}};
    Field<TextField<>> name{{.value = "abcdef"}};
    WidgetId level_id = 0, name_id = 0;
    std::vector<nodes::Port*> ports() { return {}; }
    void view(WidgetTree::ViewBuilder& vb) {
        level_id = vb.widget(level).node_ref.id;
        name_id = vb.widget(name).node_ref.id;
    }
};

struct LevelBoard {
    Level node;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(node, {.title = "Level", .family = "math", .at = P(20, 30)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

using LevelFixture = BasicFixture<LevelBoard>;

LevelFixture& zoomed(LevelFixture& f, float zoom) {
    f.m.graph.set_view({.pan = {}, .zoom = zoom});
    f.frame();
    return f;
}

std::optional<Rect> drawn_with(const SceneSnapshot& snap, WidgetId id, Color c) {
    for (std::size_t i = 0; i < snap.geometry.size(); ++i)
        if (snap.geometry[i].first == id)
            for (auto& cmd : snap.draw_lists[i]->commands)
                if (auto* r = std::get_if<FilledRect>(&cmd); r && r->color == c) return r->rect;
    return std::nullopt;
}
}

TEST_CASE("a slider in a node at zoom 0.5 follows the pointer across its drawn track") {
    LevelFixture f;
    zoomed(f, 0.5f);
    const WidgetId id = f.m.node.level_id;
    const auto thumb = drawn_with(*f.snap, id, f.tree.theme().accent);
    const auto track = find_widget_rect(*f.snap, id);
    REQUIRE((thumb && track));
    const Point at = thumb->origin + Offset{DX{thumb->extent.w.raw() / 2}, DY{thumb->extent.h.raw() / 2}};
    f.press(at);
    const double pressed = f.m.node.level.get().value;
    CHECK(pressed == doctest::Approx(0.25).epsilon(0.1));
    f.move_to(at + Offset{DX{track->extent.w.raw() / 2}, DY{0}});
    CHECK(f.m.node.level.get().value - pressed == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("a click in a text field in a node at zoom 2 puts the caret between the letters under it") {
    LevelFixture f;
    zoomed(f, 2.f);
    const auto text = f.text_at("abcdef");
    REQUIRE(text);
    const float abc = text_width(f.tree.theme().measurer, "abc", 14.f);
    const Point at{text->x + DX{2.f * abc}, text->y + DY{6.f}};
    f.press(at);
    f.release(at);
    REQUIRE(f.tree.focused_id() == f.m.node.name_id);
    route_event(f.tree, f.snap.get(), TextInput{"X"});
    CHECK(f.m.node.name.get().value == "abcXdef");
}

namespace {
template <class F>
F& at_zoom(F& f, float zoom) {
    f.m.graph.set_view({.pan = {}, .zoom = zoom});
    f.frame();
    return f;
}

float bottom(Rect r) { return r.origin.y.raw() + r.extent.h.raw(); }

template <class F>
Rect laid_out(const F& f, WidgetId id) {
    auto r = f.tree.laid_out_at(id);
    REQUIRE(r);
    return *r;
}

// Where a port's wires end, from where its card and row were laid out.
template <class F>
Point dot_of(const F& f, WidgetId label, bool output) {
    const Rect card = laid_out(f, f.card_of(label));
    const Rect row = laid_out(f, *f.tree.parent_of(label));
    return P(output ? right(card) : card.origin.x.raw(), middle_y(row));
}
}

TEST_CASE("at zoom 0.5 a card at graph point (200, 100) is laid out at (100, 50), at half its size") {
    Fixture f;
    f.m.graph.move(f.m.b, P(200, 100));
    f.frame();
    const WidgetId card = f.card_of(f.m.sink.in_label);
    const Rect natural = laid_out(f, card);
    REQUIRE(natural.origin == P(200, 100));   // the board is at the window's origin
    at_zoom(f, 0.5f);
    const Rect zoomed_card = laid_out(f, card);
    CHECK(zoomed_card.origin.x.raw() == doctest::Approx(100));
    CHECK(zoomed_card.origin.y.raw() == doctest::Approx(50));
    CHECK(zoomed_card.extent.w.raw() == doctest::Approx(natural.extent.w.raw() / 2));
    CHECK(zoomed_card.extent.h.raw() == doctest::Approx(natural.extent.h.raw() / 2));
    auto drawn = f.allocated(card);   // where the snapshot draws it
    REQUIRE(drawn);
    CHECK(drawn->origin.x.raw() == doctest::Approx(zoomed_card.origin.x.raw()));
    CHECK(drawn->extent.w.raw() == doctest::Approx(zoomed_card.extent.w.raw()));
}

TEST_CASE("at zoom 0.5 a wire runs between the zoomed cards' edges, and a drag from dot to dot connects") {
    Fixture f;
    at_zoom(f, 0.5f);
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    const Point out_dot = dot_of(f, f.m.source.out_label, true);
    const Point in_dot = dot_of(f, f.m.sink.in_label, false);
    auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].points.front().x.raw() == doctest::Approx(out_dot.x.raw()));
    CHECK(wires[0].points.front().y.raw() == doctest::Approx(out_dot.y.raw()));
    CHECK(wires[0].points.back().x.raw() == doctest::Approx(in_dot.x.raw()));
    CHECK(wires[0].points.back().y.raw() == doctest::Approx(in_dot.y.raw()));
    CHECK(wires[0].thickness == 1.f);   // 2 px at zoom 1
    auto dots = f.all<Circle>();
    CHECK(std::ranges::count_if(dots, [](const Circle& c) { return c.radius == 3.f; }) == 2);   // 2.5, floored at 3
    CHECK(std::ranges::any_of(dots, [&](const Circle& c) {
        return c.center.x.raw() == doctest::Approx(in_dot.x.raw()) && c.center.y.raw() == doctest::Approx(in_dot.y.raw());
    }));

    f.m.graph.disconnect(*f.m.graph.edge_into({f.m.b, 0}));
    f.frame();
    f.drag(out_dot + Offset{DX{2.f}, DY{0.f}}, in_dot + Offset{DX{-3.f}, DY{1.f}});
    CHECK(f.m.graph.view().pan == P(0, 0));
    auto e = f.m.graph.edge_into({f.m.b, 0});
    REQUIRE(e);
    CHECK(f.m.graph.edge(*e)->from == nodes::PortRef{f.m.a, 0});
}

TEST_CASE("at zoom 0.5 a port's whole row takes a wire, from it and onto it") {
    Fixture f;
    at_zoom(f, 0.5f);
    const Rect out = laid_out(f, f.m.source.out_label);
    const Rect in = laid_out(f, f.m.sink.in_label);
    const Rect in_row = laid_out(f, *f.tree.parent_of(f.m.sink.in_label));
    const Point from = P(out.origin.x.raw() - 10, middle_y(out));   // beside the name, in the card
    const Point to = P(right(in) + 10, middle_y(in));
    REQUIRE(to.x.raw() < right(in_row));
    f.press(from);
    f.move_to(to);
    CHECK(hinted(f, f.m.sink.in_label));   // the row's zoomed rect
    f.release(to);
    auto e = f.m.graph.edge_into({f.m.b, 0});
    REQUIRE(e);
    CHECK(f.m.graph.edge(*e)->from == nodes::PortRef{f.m.a, 0});
}

TEST_CASE("zooming out and back keeps every widget, its state and the focus: nothing is rebuilt") {
    LevelFixture f;
    const auto ids = f.tree.leaf_ids();
    const Point name = centre(f, f.m.node.name_id);
    f.press(name);
    f.release(name);
    REQUIRE(f.tree.focused_id() == f.m.node.name_id);
    f.m.node.level.set({.value = 0.75});
    f.frame();
    at_zoom(f, 0.5f);
    CHECK(f.tree.leaf_ids() == ids);
    at_zoom(f, 1.f);
    CHECK(f.tree.leaf_ids() == ids);
    CHECK(f.tree.focused_id() == f.m.node.name_id);
    CHECK(f.m.node.level.get().value == 0.75);
}

TEST_CASE("at zoom 0.5 dragging the empty board by 40 px pans by 80 board units") {
    Fixture f;
    at_zoom(f, 0.5f);
    f.drag(empty_spot, empty_spot + Offset{DX{40.f}, DY{0.f}});
    CHECK(f.m.graph.view().pan == P(80, 0));
    CHECK(f.m.graph.view().zoom == 0.5f);
}

TEST_CASE("at zoom 0.5 dragging a header by 40 px moves its node by 80") {
    Fixture f;
    at_zoom(f, 0.5f);
    auto title = f.text_at("Source");
    REQUIRE(title);
    const Point grab = *title + Offset{DX{2.f}, DY{2.f}};
    f.drag(grab, grab + Offset{DX{40.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(100));
    CHECK(f.m.graph.position(f.m.a).y.raw() == doctest::Approx(30));
}

TEST_CASE("a fit asked for shows every card inside the board") {
    Fixture f;
    f.m.graph.move(f.m.b, P(1500, 1000));
    f.frame();
    f.m.graph.request_fit();
    f.frame();
    CHECK_FALSE(f.m.graph.fit_requested());
    CHECK(f.m.graph.view().zoom < 1.f);
    for (const WidgetId label : {f.m.source.out_label, f.m.sink.in_label}) {
        const WidgetId card = f.card_of(label);
        const Rect board = laid_out(f, *f.tree.parent_of(card));
        const Rect r = laid_out(f, card);
        CHECK(board.contains(r.origin));
        CHECK(board.contains(P(right(r) - 1, bottom(r) - 1)));
    }
}

TEST_CASE("a fit asked for before the board was ever laid out waits for its size") {
    Editor m;
    m.graph.move(m.b, P(1500, 1000));
    m.graph.request_fit();
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(800, 600, 1);
    snap = tree.build_snapshot(800, 600, 2);
    CHECK_FALSE(m.graph.fit_requested());
    CHECK(m.graph.view().zoom < 1.f);
}

namespace {
enum class Wave { Sine, Square, Saw };

struct Shaper {   // a node with a dropdown and a context menu
    Field<Wave> wave{Wave::Sine};
    Field<Label<>> note{{"shape"}};
    Field<ContextMenu> menu{{.items = {{"Copy"}, {"Paste"}, {"Select all"}}}};
    WidgetId wave_id = 0, note_id = 0;
    std::vector<nodes::Port*> ports() { return {}; }
    void view(WidgetTree::ViewBuilder& vb) {
        wave_id = vb.widget(wave).node_ref.id;
        note_id = vb.widget(note).node_ref.id;
        vb.context_menu(menu);
    }
};

struct ShaperBoard {
    Shaper node;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(node, {.title = "Shaper", .family = "math", .at = P(20, 30)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

using ShaperFixture = BasicFixture<ShaperBoard>;

std::optional<Rect> open_list(const ShaperFixture& f) {
    for (auto& c : f.snap->overlay.commands)
        if (auto* r = std::get_if<FilledRect>(&c); r && r->color == f.tree.theme().popup_bg) return r->rect;
    return std::nullopt;
}

void click(ShaperFixture& f, Point at) {
    f.press(at);
    f.release(at);
}
}

TEST_CASE("at zoom 0.5 a dropdown in a node opens its list under its box, and a click picks the item there") {
    ShaperFixture f;
    at_zoom(f, 0.5f);
    const Rect box = laid_out(f, f.m.node.wave_id);
    click(f, centre(f, f.m.node.wave_id));
    auto list = open_list(f);
    REQUIRE(list);
    CHECK(list->origin.x.raw() == doctest::Approx(box.origin.x.raw()));
    CHECK(list->origin.y.raw() == doctest::Approx(bottom(box)));
    CHECK(list->extent.w.raw() == doctest::Approx(box.extent.w.raw()));
    const float row = list->extent.h.raw() / 3;
    click(f, P(list->origin.x.raw() + 10, list->origin.y.raw() + 1.5f * row));
    CHECK(f.m.node.wave.get() == Wave::Square);
}

TEST_CASE("at zoom 0.5 a dropdown near the window's bottom opens below while its zoomed list fits there") {
    ShaperFixture f;
    at_zoom(f, 0.5f);
    const Rect box = laid_out(f, f.m.node.wave_id);
    const float list_h = 3 * 28.f * 0.5f;
    const float top = 600.f - list_h - box.extent.h.raw() - 4.f;   // the list just fits below
    f.m.graph.move(f.m.a, f.m.graph.position(f.m.a) + Offset{DX{0}, DY{(top - box.origin.y.raw()) / 0.5f}});
    f.frame();
    const Rect moved = laid_out(f, f.m.node.wave_id);
    REQUIRE(moved.origin.y.raw() == doctest::Approx(top));
    click(f, centre(f, f.m.node.wave_id));
    auto list = open_list(f);
    REQUIRE(list);
    CHECK(list->origin.y.raw() == doctest::Approx(bottom(moved)));
    CHECK(list->extent.h.raw() == doctest::Approx(list_h));
}

TEST_CASE("at zoom 0.5 a node's context menu opens at the pointer, and a click on its second row chooses it") {
    ShaperFixture f;
    at_zoom(f, 0.5f);
    const Point at = centre(f, f.m.node.note_id);
    route_event(f.tree, f.snap.get(), MouseButton{at, buttons::right, true});
    route_event(f.tree, f.snap.get(), MouseButton{at, buttons::right, false});
    f.frame();
    REQUIRE(f.tree.popup_owner() != 0);
    auto area = app::widget_detail::popup_area(f.tree, *f.snap);
    REQUIRE(area);
    CHECK(area->origin.x.raw() == doctest::Approx(at.x.raw()));
    CHECK(area->origin.y.raw() == doctest::Approx(at.y.raw()));
    click(f, at + Offset{DX{5.f}, DY{(28.f + 10.f) * 0.5f}});
    CHECK(f.tree.popup_owner() == 0);
    CHECK(f.m.node.menu.get().chosen == std::optional<std::size_t>{1});
}
