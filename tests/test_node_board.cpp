// vb.node_board: a graph shown as cards on a board, its wires drawn from card edge to card edge.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/nodes/graph.hpp>
#include <prism/nodes/node_kind.hpp>
#include <prism/nodes/tidy.hpp>

#include <prism/ui/text_metrics.hpp>

#include <algorithm>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;
namespace nodes = prism::nodes;

// The theme names a colour per wire slot the node registry can give out.
static_assert(prism::ui::node_wire_slots == prism::nodes::wire_slots);

namespace {

Point P(float x, float y) { return {X{x}, Y{y}}; }

// The rounded squares drawn centred on `at`: a port's dot and the ring of board around it.
std::vector<RoundedRect> centred_on(const std::vector<RoundedRect>& rects, Point at) {
    std::vector<RoundedRect> out;
    for (const auto& r : rects)
        if (r.rect.origin.x.raw() + r.rect.extent.w.raw() / 2.f == doctest::Approx(at.x.raw())
            && r.rect.origin.y.raw() + r.rect.extent.h.raw() / 2.f == doctest::Approx(at.y.raw()))
            out.push_back(r);
    return out;
}

// The wire leaving the dot at `from`: the polyline starting there.
std::optional<Polyline> leaving(const std::vector<Polyline>& wires, Point from) {
    for (const auto& w : wires)
        if (w.points.front().x.raw() == doctest::Approx(from.x.raw())
            && w.points.front().y.raw() == doctest::Approx(from.y.raw()))
            return w;
    return std::nullopt;
}

// `c` at a fraction of its own alpha: how a wire's ports fade while it is dragged.
Color faded(Color c, float by) {
    c.a = static_cast<std::uint8_t>(float(c.a) * by + 0.5f);
    return c;
}

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
    // What widget `id` itself draws, one shape at a time.
    template <typename Cmd>
    std::vector<Cmd> draws_of(WidgetId id) const {
        std::vector<Cmd> out;
        for (std::size_t i = 0; i < snap->geometry.size(); ++i)
            if (snap->geometry[i].first == id)
                for (auto& c : snap->draw_lists[i]->commands)
                    if (auto* x = std::get_if<Cmd>(&c)) out.push_back(*x);
        return out;
    }
    std::optional<TextCmd> text_cmd(std::string_view text) const {
        for (auto& t : all<TextCmd>()) if (t.text == text) return t;
        return std::nullopt;
    }
    // Whether widget `id` paints anything of colour `c`, in any of the shapes a grid is drawn in.
    bool paints(WidgetId id, Color c) const {
        return std::ranges::any_of(draws_of<Line>(id), [&](const Line& l) { return l.color == c; })
            || std::ranges::any_of(draws_of<Polyline>(id), [&](const Polyline& p) { return p.color == c; })
            || std::ranges::any_of(draws_of<FilledRect>(id), [&](const FilledRect& r) { return r.color == c; })
            || std::ranges::any_of(draws_of<RectOutline>(id), [&](const RectOutline& o) { return o.color == c; })
            || std::ranges::any_of(draws_of<Circle>(id), [&](const Circle& d) { return d.color == c; });
    }
    // The board's background: the canvas it builds before its cards, so the first leaf below it.
    WidgetId background_of(WidgetId board) const {
        for (WidgetId id : tree.leaf_ids())
            if (tree.within(id, board)) return id;
        return 0;
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
    CHECK(f.shows("SOURCE"));
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
    CHECK(wires[0].color == f.tree.theme().node_wires[nodes::wire_slot(f.m.source.out.type())]);
    auto first = wires[0].points.front(), last = wires[0].points.back();
    CHECK(first.x.raw() == doctest::Approx(right(*out_card)));
    CHECK(first.y.raw() == doctest::Approx(middle_y(*out_row)));
    CHECK(last.x.raw() == doctest::Approx(in_card->origin.x.raw()));
    CHECK(last.y.raw() == doctest::Approx(middle_y(*in_row)));

    // A dot on each end of it, in the port's colour.
    const Color number = f.tree.theme().node_wires[nodes::wire_slot(f.m.source.out.type())];
    CHECK(std::ranges::any_of(centred_on(f.all<RoundedRect>(), first), [&](const RoundedRect& r) {
        return r.color == number && r.thickness == 0.f;
    }));
    CHECK(std::ranges::any_of(centred_on(f.all<RoundedRect>(), last), [&](const RoundedRect& r) {
        return r.color == number && r.thickness == 0.f;
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

// The sink out of the source's reach: a node dragged near it would snap to it.
template <class F>
void park_sink(F& f, Point at = P(700, 500)) {
    f.m.graph.move(f.m.b, at);
    f.frame();
}
}

TEST_CASE("dragging a header by (30, 20) moves its node by (30, 20)") {
    Fixture f;
    park_sink(f);
    auto ids = f.tree.leaf_ids();
    auto title = f.text_at("Source");
    REQUIRE(title);
    const Point grab = *title + Offset{DX{4.f}, DY{4.f}};
    f.drag(grab, grab + Offset{DX{30.f}, DY{20.f}});
    CHECK(f.m.graph.position(f.m.a) == P(50, 50));
    CHECK(f.m.graph.position(f.m.b) == P(700, 500));
    auto moved = f.text_at("Source");
    REQUIRE(moved);
    CHECK(moved->x.raw() == doctest::Approx(title->x.raw() + 30));
    CHECK(moved->y.raw() == doctest::Approx(title->y.raw() + 20));
    CHECK(f.tree.leaf_ids() == ids);
}

TEST_CASE("the header's family label takes the drag too, and a second drag goes on from there") {
    Fixture f;
    park_sink(f);
    auto family = f.text_at("SOURCE");
    REQUIRE(family);
    const Point grab = *family + Offset{DX{4.f}, DY{4.f}};
    f.drag(grab, grab + Offset{DX{10.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a) == P(30, 30));
    const Point on_family = *f.text_at("SOURCE") + Offset{DX{4.f}, DY{4.f}};
    f.drag(on_family, on_family + Offset{DX{0.f}, DY{10.f}});
    CHECK(f.m.graph.position(f.m.a) == P(30, 40));
}

TEST_CASE("moves read against a frame that shows the node where it was do not overshoot") {
    Fixture f;
    park_sink(f);
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
    park_sink(f);
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
    CHECK(trails[0].color == f.tree.theme().node_wires[nodes::wire_slot(f.m.source.out.type())]);
    CHECK(trails[0].thickness == doctest::Approx(2.f));   // a number wire's, at zoom 1
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
    REQUIRE(wires.size() == 2);          // the wire, and the faint wider stroke under it
    const auto under = std::ranges::find_if(wires, [](const Polyline& w) { return w.thickness == doctest::Approx(9.f); });
    const auto wire = std::ranges::find_if(wires, [](const Polyline& w) { return w.thickness == doctest::Approx(2.f); });
    REQUIRE((under != wires.end() && wire != wires.end()));   // selected: the wire itself stays thin
    CHECK(under->color == Color::rgba(wire->color.r, wire->color.g, wire->color.b, 64));
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
        return r && std::ranges::any_of(f.all<RoundedRect>(), [&](const RoundedRect& o) {
            return o.color == f.tree.theme().node_border_selected && o.rect.origin == r->origin
                && o.thickness > 0.f && o.radius == doctest::Approx(10.f);
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
    park_sink(f);
    f.drag(grab, grab + Offset{DX{30.f}, DY{20.f}});
    park_sink(f, P(300, 60));
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
    const auto squares = centred_on(f.all<RoundedRect>(), in_dot);
    const auto face = std::ranges::find_if(squares, [](const RoundedRect& r) {
        return r.thickness == 0.f && r.radius == doctest::Approx(1.5f);
    });
    REQUIRE(face != squares.end());
    CHECK(face->rect.extent.w.raw() == doctest::Approx(6.f));   // 12 px at zoom 1

    f.m.graph.disconnect(*f.m.graph.edge_into({f.m.b, 0}));
    f.frame();
    f.drag(out_dot + Offset{DX{2.f}, DY{0.f}}, in_dot + Offset{DX{-3.f}, DY{1.f}});
    CHECK(f.m.graph.view().pan == P(0, 0));
    auto e = f.m.graph.edge_into({f.m.b, 0});
    REQUIRE(e);
    CHECK(f.m.graph.edge(*e)->from == nodes::PortRef{f.m.a, 0});
}

TEST_CASE("at zoom 0.5 the selected node's outline follows the card's own corner radius") {
    Fixture f;
    at_zoom(f, 0.5f);
    const Point header = *f.text_at("Sink") + Offset{DX{2.f}, DY{2.f}};
    f.press(header);
    f.release(header);
    const WidgetId card = f.card_of(f.m.sink.in_label);
    const Rect drawn = *f.allocated(card);
    const auto rects = f.all<RoundedRect>();
    const auto border = std::ranges::find_if(rects, [&](const RoundedRect& o) {
        return o.color == f.tree.theme().node_border_selected && o.rect.origin == drawn.origin;
    });
    REQUIRE(border != rects.end());
    CHECK(border->radius == doctest::Approx(5.f));   // 10 px at zoom 1
    const auto ring = std::ranges::find_if(rects, [&](const RoundedRect& o) {
        return o.color == f.tree.theme().node_glow;
    });
    REQUIRE(ring != rects.end());
    CHECK(ring->radius == doctest::Approx(5.f + 1.f));   // grown by 2 px at zoom 1
    CHECK(ring->rect.origin.x.raw() == doctest::Approx(drawn.origin.x.raw() - 1.f));
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
    park_sink(f);
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
// A heading that wraps over a narrow window: its height changes once it is measured, so the board
// below it is a different height on the first layout passes.
struct Headed {
    Field<Label<>> heading{{.value = "A sentence long enough to take more than one line in a narrow "
                                     "window, above the board rather than inside it.",
                            .wrap = true}};
    Source source;
    Sink sink;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(source, {.title = "Source", .family = "source", .at = P(20, 30)});
    nodes::NodeId b = graph.add(sink, {.title = "Sink", .family = "math", .at = P(300, 250), .removable = true});
    WidgetId board = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(heading);
            board = vb.node_board(graph);
        });
    }
};
} // namespace

TEST_CASE("a fit asked for before the first frame waits for the board's size to settle") {
    Headed m;
    m.graph.request_fit();
    WidgetTree tree{m};
    for (std::uint64_t v = 1; v <= 6; ++v) {
        auto snap = tree.build_snapshot(240, 400, v);
        static_cast<void>(snap);   // frames until well past the fit, which waits for a stable size
    }
    CHECK_FALSE(m.graph.fit_requested());
    const Rect board = *tree.laid_out_at(m.board);
    std::vector<Rect> cards;
    for (const WidgetId label : {m.source.out_label, m.sink.in_label})
        cards.push_back(*tree.laid_out_at(*tree.parent_of(*tree.parent_of(label))));
    const float zoom = m.graph.view().zoom;
    const auto natural = [&](const Rect& r) { return Size{r.extent.w / zoom, r.extent.h / zoom}; };
    const float settled_zoom = nodes::fit_all(std::vector<Rect>{{m.graph.position(m.a), natural(cards[0])},
                                                                 {m.graph.position(m.b), natural(cards[1])}},
                                              board.extent).zoom;
    CHECK(zoom == doctest::Approx(settled_zoom));
    for (const Rect& r : cards) {
        CHECK(board.contains(r.origin));
        CHECK(board.contains(Point{X{right(r) - 1.f}, Y{bottom(r) - 1.f}}));
    }
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
    CHECK(list->extent.w.raw() == doctest::Approx(box.extent.w.raw() / 0.5f));   // at natural size
    const float row = list->extent.h.raw() / 3;
    click(f, P(list->origin.x.raw() + 10, list->origin.y.raw() + 1.5f * row));
    CHECK(f.m.node.wave.get() == Wave::Square);
}

TEST_CASE("at zoom 0.25 a dropdown's list is drawn at natural size just below its zoomed box, and a click picks its second item") {
    ShaperFixture f;
    at_zoom(f, 0.25f);
    const Rect box = laid_out(f, f.m.node.wave_id);
    click(f, centre(f, f.m.node.wave_id));
    auto list = open_list(f);
    REQUIRE(list);
    CHECK(list->origin.x.raw() == doctest::Approx(box.origin.x.raw()));
    CHECK(list->origin.y.raw() == doctest::Approx(bottom(box)));
    CHECK(list->extent.w.raw() == doctest::Approx(box.extent.w.raw() / 0.25f));
    CHECK(list->extent.h.raw() == doctest::Approx(3 * 28.f));
    std::optional<float> size;
    for (auto& c : f.snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "Square") size = t->size;
    REQUIRE(size);
    CHECK(*size == doctest::Approx(14.f));
    click(f, P(list->origin.x.raw() + 10, list->origin.y.raw() + 1.5f * 28.f));
    CHECK(f.m.node.wave.get() == Wave::Square);
}

TEST_CASE("at zoom 0.5 a dropdown near the window's bottom opens below while its list fits there") {
    ShaperFixture f;
    at_zoom(f, 0.5f);
    const Rect box = laid_out(f, f.m.node.wave_id);
    const float list_h = 3 * 28.f;   // at natural size
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
    click(f, at + Offset{DX{5.f}, DY{28.f + 10.f}});   // the menu at natural size
    CHECK(f.tree.popup_owner() == 0);
    CHECK(f.m.node.menu.get().chosen == std::optional<std::size_t>{1});
}

namespace {
struct Options {   // a node with a button and a popup holding a checkbox
    Field<Button> more{{"more"}};
    Field<Popup> pop{{.open = false, .modal = false}};
    Field<Checkbox> loud{{.checked = false, .label = "loud"}};
    WidgetId loud_id = 0;
    std::vector<nodes::Port*> ports() { return {}; }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.widget(more);
        vb.popup(pop, [&](WidgetTree::ViewBuilder& p) { loud_id = p.widget(loud).node_ref.id; });
    }
};

struct OptionsBoard {
    Options node;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(node, {.title = "Options", .family = "math", .at = P(200, 100)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};
}

TEST_CASE("at zoom 0.5 a popup in a node opens next to its anchor's zoomed place, and its widgets take clicks") {
    BasicFixture<OptionsBoard> f;
    at_zoom(f, 0.5f);
    f.m.node.pop.set({.open = true, .modal = false});
    f.frame();
    REQUIRE(f.tree.popup_node());
    const Rect anchor = laid_out(f, f.tree.popup_node()->id);
    const auto card = f.tree.popup_card();
    REQUIRE(card);
    CHECK(card->origin.x.raw() == doctest::Approx(anchor.origin.x.raw()));
    CHECK(card->origin.y.raw() == doctest::Approx(anchor.origin.y.raw()));
    const auto area = app::widget_detail::popup_area(f.tree, *f.snap);
    REQUIRE(area);
    CHECK(area->origin == card->origin);

    Rect box{};
    for (auto& [id, r] : f.snap->overlay_geometry)
        if (id == f.m.node.loud_id) box = r;
    REQUIRE(box.extent.w.raw() > 0.f);
    REQUIRE(card->contains(box.origin));
    const Point on_box = box.origin + Offset{DX{box.extent.w.raw() / 2}, DY{box.extent.h.raw() / 2}};
    f.press(on_box);
    f.release(on_box);
    CHECK(f.m.node.loud.get().checked);
    CHECK(f.m.node.pop.get().open);
    REQUIRE(f.tree.popup_card());
    CHECK(f.tree.popup_card()->origin == card->origin);   // it stays where it opened
}

namespace {
struct Tally {   // a spin box steps on the wheel
    Field<SpinBox<int>> count{{.value = 5}};
    WidgetId count_id = 0;
    std::vector<nodes::Port*> ports() { return {}; }
    void view(WidgetTree::ViewBuilder& vb) { count_id = vb.widget(count).node_ref.id; }
};

struct TallyBoard {
    Tally node;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(node, {.title = "Tally", .family = "math", .at = P(20, 30)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

template <class F>
void wheel(F& f, Point at, float notches, std::uint16_t with = 0) {
    route_event(f.tree, f.snap.get(), MouseScroll{at, DX{0.f}, DY{notches}, with});
    f.frame();
}

template <class F>
void middle_drag(F& f, Point from, Offset by) {
    route_event(f.tree, f.snap.get(), MouseButton{from, buttons::middle, true});
    f.frame();
    route_event(f.tree, f.snap.get(), MouseMove{from + by, 2});
    f.frame();
    route_event(f.tree, f.snap.get(), MouseButton{from + by, buttons::middle, false});
    f.frame();
}
}

TEST_CASE("Ctrl+wheel over a node's slider zooms the board about the pointer and leaves the slider alone") {
    LevelFixture f;
    const WidgetId id = f.m.node.level_id;
    const Point p = centre(f, id);
    const Rect before = laid_out(f, id);
    wheel(f, p, 1.f, mods::ctrl);
    CHECK(f.m.graph.view().zoom == doctest::Approx(1.1f));
    CHECK(f.m.node.level.get().value == 0.25);
    const Rect after = laid_out(f, id);   // the board is at the window's origin
    CHECK(after.origin.x.raw() == doctest::Approx(p.x.raw() + (before.origin.x.raw() - p.x.raw()) * 1.1f));
    CHECK(after.origin.y.raw() == doctest::Approx(p.y.raw() + (before.origin.y.raw() - p.y.raw()) * 1.1f));
}

TEST_CASE("Ctrl+wheel over the empty board zooms too, about the pointer") {
    LevelFixture f;
    const Rect before = laid_out(f, f.m.node.level_id);
    wheel(f, empty_spot, -1.f, mods::ctrl);
    CHECK(f.m.graph.view().zoom == doctest::Approx(1.f / 1.1f));
    const Rect after = laid_out(f, f.m.node.level_id);
    const Point p = empty_spot;
    CHECK(after.origin.x.raw() == doctest::Approx(p.x.raw() + (before.origin.x.raw() - p.x.raw()) / 1.1f));
    CHECK(after.origin.y.raw() == doctest::Approx(p.y.raw() + (before.origin.y.raw() - p.y.raw()) / 1.1f));
}

TEST_CASE("a wheel without Ctrl steps a node's spin box and zooms nothing; with Ctrl it zooms and steps nothing") {
    BasicFixture<TallyBoard> f;
    const Point p = centre(f, f.m.node.count_id);
    wheel(f, p, 1.f);
    CHECK(f.m.node.count.get().value == 6);
    CHECK(f.m.graph.view().zoom == 1.f);
    wheel(f, centre(f, f.m.node.count_id), 1.f, mods::ctrl);
    CHECK(f.m.node.count.get().value == 6);
    CHECK(f.m.graph.view().zoom == doctest::Approx(1.1f));
}

TEST_CASE("ten Ctrl+wheel notches up end at zoom 2, thirty down at 0.25") {
    LevelFixture f;
    for (int i = 0; i < 10; ++i) wheel(f, empty_spot, 1.f, mods::ctrl);
    CHECK(f.m.graph.view().zoom == 2.f);
    for (int i = 0; i < 30; ++i) wheel(f, empty_spot, -1.f, mods::ctrl);
    CHECK(f.m.graph.view().zoom == 0.25f);
}

TEST_CASE("a middle drag on a node's slider pans the board and leaves the slider; a left drag still moves it") {
    LevelFixture f;
    const WidgetId id = f.m.node.level_id;
    middle_drag(f, centre(f, id), Offset{DX{30.f}, DY{0.f}});
    CHECK(f.m.graph.view().pan == P(30, 0));
    CHECK(f.m.node.level.get().value == 0.25);
    CHECK(f.tree.captured_id() == 0);

    const Point at = centre(f, id);
    const auto track = find_widget_rect(*f.snap, id);
    REQUIRE(track);
    f.drag(at, at + Offset{DX{track->extent.w.raw() / 4}, DY{0.f}});
    CHECK(f.m.node.level.get().value > 0.5);
    CHECK(f.m.graph.view().pan == P(30, 0));
}

TEST_CASE("at zoom 0.5 a middle drag by 30 px over a card pans by 60 board units") {
    LevelFixture f;
    zoomed(f, 0.5f);
    middle_drag(f, centre(f, f.m.node.level_id), Offset{DX{30.f}, DY{0.f}});
    CHECK(f.m.graph.view().pan == P(60, 0));
    CHECK(f.m.graph.view().zoom == 0.5f);
}

TEST_CASE("a middle click in the middle of a left pan does not stop it") {
    Fixture f;
    f.press(empty_spot);
    f.move_to(empty_spot + Offset{DX{-10.f}, DY{0.f}});
    const Point here = empty_spot + Offset{DX{-10.f}, DY{0.f}};
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::middle, true});
    route_event(f.tree, f.snap.get(), MouseButton{here, buttons::middle, false});
    f.frame();
    f.move_to(empty_spot + Offset{DX{-30.f}, DY{-20.f}});
    CHECK(f.m.graph.view().pan == P(-30, -20));
    f.release(empty_spot + Offset{DX{-30.f}, DY{-20.f}});
}

namespace {
// The gallery's Nodes tab: a label showing the zoom, read from the graph's view.
struct ZoomShown : Editor {
    Derived<std::string> zoom_label{[this] {
        return std::to_string(static_cast<int>(graph.view().zoom * 100.f + 0.5f)) + " %";
    }};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.widget(zoom_label);
        vb.node_board(graph);
    }
};

// What the app does each frame: build, then clear the marks the frame drew.
template <class F>
void publish(F& f) {
    f.frame();
    f.tree.clear_dirty();
}
}

TEST_CASE("a fit made during a publish leaves the tree dirty, so what shows the view is drawn again") {
    BasicFixture<ZoomShown> f;
    f.m.graph.set_view({.pan = {}, .zoom = 0.51f});
    publish(f);
    REQUIRE(f.shows("51 %"));
    f.m.graph.request_fit();
    publish(f);   // the board fits in this one's layout
    REQUIRE(f.m.graph.view().zoom == 1.f);
    CHECK(f.tree.any_dirty());   // the app publishes again
    publish(f);
    CHECK(f.shows("100 %"));
}

namespace {
struct Tabbed {   // a node holding tabs that can be dragged along their bar
    List<std::string> open;
    Field<TabBar<>> current;
    Field<Label<>> body{{"body"}};
    Tabbed() { for (auto name : {"one", "two", "three"}) open.push_back(name); }
    std::vector<nodes::Port*> ports() { return {}; }
    void view(WidgetTree::ViewBuilder& vb) {
        vb.tabs(current, open, [](const std::string& s) { return s; },
                [this](WidgetTree::ViewBuilder& p, const std::string&) { p.widget(body); })
            .reorderable();
    }
};

struct TabbedBoard {
    Tabbed node;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(node, {.title = "Tabbed", .family = "math", .at = P(20, 30)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

std::vector<Rect> overlay_rects(const SceneSnapshot& snap, Color c) {
    std::vector<Rect> out;
    for (auto& cmd : snap.overlay.commands)
        if (auto* r = std::get_if<FilledRect>(&cmd); r && r->color == c) out.push_back(r->rect);
    return out;
}
}

TEST_CASE("at zoom 2 a tab dragged along a node's bar shows the insertion bar between the tabs under the pointer, zoomed") {
    BasicFixture<TabbedBoard> f;
    at_zoom(f, 2.f);
    const auto one = f.text_at("one"), two = f.text_at("two"), three = f.text_at("three");
    REQUIRE((one && two && three));
    const Point from = *three + Offset{DX{5}, DY{5}};
    const Point to = *two + Offset{DX{2}, DY{5}};   // the left part of "two": before it
    f.press(from);
    f.move_to(from - Offset{DX{20}, DY{0}});
    f.move_to(to);
    REQUIRE(f.tree.dragging());
    const float one_end = one->x.raw() + 2.f * text_width(f.tree.theme().measurer, "one", 14.f);
    const auto bars = overlay_rects(*f.snap, f.tree.theme().accent);
    const auto bar = std::ranges::find_if(bars, [](const Rect& r) { return r.extent.w.raw() == doctest::Approx(4.f); });
    REQUIRE(bar != bars.end());   // 2 px wide at zoom 1
    const float x = bar->origin.x.raw() + 2.f;
    CHECK(x > one_end);
    CHECK(x < two->x.raw());
    f.release(to);
    CHECK(std::vector<std::string>(f.m.node.open.begin(), f.m.node.open.end())
          == std::vector<std::string>{"one", "three", "two"});
}

TEST_CASE("a zoom asked for is made about the board's centre at its next layout") {
    Fixture f;
    f.m.graph.set_view({.pan = {}, .zoom = 0.5f});
    f.frame();
    const Rect board = laid_out(f, *f.tree.parent_of(f.card_of(f.m.sink.in_label)));
    f.m.graph.request_zoom(1.f);
    f.frame();
    CHECK(f.m.graph.view().zoom == 1.f);
    const float cx = board.extent.w.raw() / 2, cy = board.extent.h.raw() / 2;   // was graph point (2cx, 2cy)
    CHECK(f.m.graph.view().pan.x.raw() == doctest::Approx(cx - 2 * cx));
    CHECK(f.m.graph.view().pan.y.raw() == doctest::Approx(cy - 2 * cy));
}

TEST_CASE("only what the last build changed is kept dirty: builds not followed by a clear do not pile up") {
    BasicFixture<ZoomShown> f;
    f.m.graph.set_view({.pan = {}, .zoom = 0.51f});
    f.m.graph.request_fit();
    f.frame();   // the fit is made in this build's layout
    REQUIRE(f.m.graph.view().zoom == 1.f);
    CHECK_FALSE(f.tree.clear_dirty().empty());
    f.m.graph.request_zoom(0.5f);
    f.frame();   // changes the view in its layout
    f.frame();   // changes nothing
    CHECK(f.tree.clear_dirty().empty());
}

namespace {
// A board alone in the window, one node in family `family`: the board's background is its first
// leaf, and the whole of the window is board to look at.
struct OneNode {
    explicit OneNode(std::string family_name = "source", std::string title = "Node")
        : family(std::move(family_name)),
          title_text(std::move(title)),
          a(graph.add(source, {.title = this->title_text, .family = this->family, .at = P(20, 30)})) {}
    std::string family;
    std::string title_text;
    Source source;
    nodes::Graph graph;
    nodes::NodeId a;
    WidgetId board = 0;
    void view(WidgetTree::ViewBuilder& vb) { board = vb.node_board(graph); }
};

// The same, in a family the theme's table does not name.
struct UnknownFamily : OneNode {
    UnknownFamily() : OneNode("mystery") {}
};

// A long title in a wide family name: the header has to make room for both.
struct LongTitle : OneNode {
    LongTitle() : OneNode("display", "Display settings") {}
};

using OneNodeFixture = BasicFixture<OneNode>;
using UnknownFamilyFixture = BasicFixture<UnknownFamily>;
using LongTitleFixture = BasicFixture<LongTitle>;

Color family_color(const Theme& t, std::string_view name) {
    for (const auto& f : t.node_families)
        if (f.name == name) return f.color;
    return t.node_family_default;
}

bool inside(Rect outer, Rect inner) {
    return inner.origin.x.raw() >= outer.origin.x.raw() - 0.5f
        && inner.origin.y.raw() >= outer.origin.y.raw() - 0.5f
        && right(inner) <= right(outer) + 0.5f
        && inner.origin.y.raw() + inner.extent.h.raw() <= outer.origin.y.raw() + outer.extent.h.raw() + 0.5f;
}

std::vector<Circle> circles_of(const std::vector<Circle>& what, Color c) {
    std::vector<Circle> out;
    for (const auto& d : what) if (d.color == c) out.push_back(d);
    return out;
}

// Every x a row of dots sits on, in order.
std::vector<float> columns_of(const std::vector<Circle>& dots) {
    std::vector<float> xs;
    for (const auto& d : dots) xs.push_back(d.center.x.raw());
    std::ranges::sort(xs);
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    return xs;
}
}

TEST_CASE("a long title stops before the header's family badge") {
    LongTitleFixture f;
    const auto title = f.text_cmd("Display settings");
    const auto badge = f.text_cmd("DISPLAY");
    REQUIRE((title && badge));
    const float title_right = title->origin.x.raw()
                            + text_width(f.tree.theme().measurer, title->text, title->size);
    CHECK(title_right <= badge->origin.x.raw());
}

TEST_CASE("a port's label sits on the card, with no box of its own") {
    Fixture f;
    const Color box = f.tree.theme().surface;
    const auto fills = f.draws_of<FilledRect>(f.m.source.out_label);
    CHECK(std::ranges::none_of(fills, [&](const FilledRect& r) { return r.color == box; }));
}

TEST_CASE("a port's label clears the dot that lies over its card's edge") {
    Fixture f;
    const Theme& t = f.tree.theme();
    // Half a dot, its ring, and a little air: what an edge steals from the card at zoom 1.
    constexpr float dot_clearance = 11.f;

    const auto out = f.text_cmd("out");
    REQUIRE(out);
    const Rect out_card = *f.allocated(f.card_of(f.m.source.out_label));
    const float out_right = out->origin.x.raw() + text_width(t.measurer, out->text, out->size);
    CHECK(out_right <= right(out_card) - dot_clearance);

    const auto in = f.text_cmd("in");
    REQUIRE(in);
    const Rect in_card = *f.allocated(f.card_of(f.m.sink.in_label));
    CHECK(in->origin.x.raw() >= in_card.origin.x.raw() + dot_clearance);
}

TEST_CASE("the board paints the theme's board colour, then a dot grid every 20 board units") {
    OneNodeFixture f;
    const Theme& t = f.tree.theme();
    const WidgetId bg = f.background_of(f.m.board);
    REQUIRE(bg != 0);

    const auto fills = f.draws_of<FilledRect>(bg);
    REQUIRE(!fills.empty());
    CHECK(fills.front().color == t.node_board_bg);   // the first thing the background draws
    const auto box = f.allocated(bg);
    REQUIRE(box);
    CHECK(fills.front().rect == *box);               // and it covers the whole of it

    const auto dots = circles_of(f.draws_of<Circle>(bg), t.node_grid);
    REQUIRE(!dots.empty());
    const auto xs = columns_of(dots);
    REQUIRE(xs.size() > 2);
    CHECK(xs[1] - xs[0] == doctest::Approx(20.f));
    CHECK(xs[xs.size() - 1] - xs[xs.size() - 2] == doctest::Approx(20.f));
}

TEST_CASE("the grid moves with the board's pan, and is left out when its dots would crowd") {
    OneNodeFixture f;
    const Theme& t = f.tree.theme();
    const WidgetId bg = f.background_of(f.m.board);
    REQUIRE(bg != 0);

    f.m.graph.set_view({.pan = P(5, 0), .zoom = 1.f});
    f.frame();
    const auto xs = columns_of(circles_of(f.draws_of<Circle>(bg), t.node_grid));
    REQUIRE(xs.size() > 1);
    CHECK(xs[0] == doctest::Approx(5.f));      // the dot on board x = 0, moved by the pan
    CHECK(xs[1] == doctest::Approx(25.f));

    f.m.graph.set_view({.pan = {}, .zoom = 0.3f});
    f.frame();
    CHECK(circles_of(f.draws_of<Circle>(bg), t.node_grid).empty());   // 20 * 0.3 = 6 px apart
}

TEST_CASE("a card is the theme's surface, its header the family colour mixed 20 % over it") {
    Fixture f;
    const Theme& t = f.tree.theme();
    const auto card = f.card_of(f.m.source.out_label);
    const auto rect = f.allocated(card);
    REQUIRE(rect);

    CHECK(std::ranges::any_of(f.all<RoundedRect>(), [&](const RoundedRect& r) {
        return r.color == t.node_surface && r.thickness == 0.f && r.radius == doctest::Approx(10.f)
            && inside(*rect, r.rect);
    }));
    CHECK(std::ranges::any_of(f.all<RoundedRect>(), [&](const RoundedRect& r) {
        return r.color == t.node_border && r.thickness == doctest::Approx(1.f) && inside(*rect, r.rect);
    }));

    const Color source = family_color(t, "source");
    const Color band = mix(t.node_surface, source, 0.2f);
    const auto bands = f.all<RoundedRect>();
    const auto header = std::ranges::find_if(bands, [&](const RoundedRect& r) {
        return r.color == band && r.thickness == 0.f;
    });
    REQUIRE(header != bands.end());
    CHECK(inside(*rect, header->rect));
    CHECK(header->rect.origin.y.raw() == doctest::Approx(rect->origin.y.raw() + 1.f));   // the card's top
    CHECK(header->rect.extent.h.raw() < rect->extent.h.raw() - 1.f);                     // not all of it
    CHECK(std::ranges::any_of(f.all<Circle>(), [&](const Circle& d) {
        return d.color == source && rect->contains(d.center);   // the header's dot, drawn by its frame
    }));

    const auto family = f.text_cmd("SOURCE");   // upper-cased, on the right
    REQUIRE(family);
    CHECK(family->color == mix(t.text, source, 0.6f));
}

TEST_CASE("a family the theme does not name is drawn in the default family colour") {
    UnknownFamilyFixture f;
    const Theme& t = f.tree.theme();
    const Color def = t.node_family_default;

    CHECK(std::ranges::any_of(f.all<RoundedRect>(), [&](const RoundedRect& r) {
        return r.color == mix(t.node_surface, def, 0.2f) && r.thickness == 0.f;
    }));
    const auto family = f.text_cmd("MYSTERY");
    REQUIRE(family);
    CHECK(family->color == mix(t.text, def, 0.6f));
}

TEST_CASE("switching the theme repaints the board in the other look, keeping every widget") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    const auto ids = f.tree.leaf_ids();
    const auto card = f.card_of(f.m.source.out_label);
    const WidgetId bg = f.background_of(*f.tree.parent_of(card));
    REQUIRE(bg != 0);
    const auto card_surface = [&](Color surface) {
        return std::ranges::any_of(f.all<RoundedRect>(), [&](const RoundedRect& r) {
            return r.color == surface && r.thickness == 0.f;
        });
    };
    const auto dark = dark_theme();
    const auto light = light_theme();
    const auto before = f.draws_of<FilledRect>(bg);
    REQUIRE(!before.empty());
    CHECK(before.front().color == dark.node_board_bg);
    CHECK(card_surface(dark.node_surface));
    CHECK_FALSE(circles_of(f.draws_of<Circle>(bg), dark.node_grid).empty());   // dots, in look A
    auto family = f.text_cmd("SOURCE");
    REQUIRE(family);
    CHECK(family->color == mix(dark.text, family_color(dark, "source"), 0.6f));

    f.tree.set_theme(light);
    f.frame();

    CHECK(f.tree.leaf_ids() == ids);   // a repaint, not a rebuild
    const auto after = f.draws_of<FilledRect>(bg);
    REQUIRE(!after.empty());
    CHECK(after.front().color == light.node_board_bg);
    CHECK(card_surface(light.node_surface));
    CHECK(f.paints(bg, light.node_grid));                                        // lines, in look B
    CHECK(circles_of(f.draws_of<Circle>(bg), light.node_grid).empty());
    family = f.text_cmd("SOURCE");
    REQUIRE(family);
    CHECK(family->color == mix(light.text, family_color(light, "source"), 0.6f));
}

namespace {
struct AppFamily : OneNode {
    AppFamily() : OneNode("ada-fx") {}
};
}

TEST_CASE("an app's family keeps its colour across theme switches, in the look the theme has") {
    const Color on_dark = Color::rgba(10, 200, 30), on_light = Color::rgba(200, 30, 10);
    set_family_color("ada-fx", on_dark, on_light);
    BasicFixture<AppFamily> f;
    const auto name_color = [&] { return f.text_cmd("ADA-FX")->color; };
    REQUIRE(f.text_cmd("ADA-FX"));
    CHECK(name_color() == mix(dark_theme().text, on_dark, 0.6f));
    f.tree.set_theme(light_theme());
    f.frame();
    CHECK(name_color() == mix(light_theme().text, on_light, 0.6f));
    f.tree.set_theme(dark_theme());
    f.frame();
    CHECK(name_color() == mix(dark_theme().text, on_dark, 0.6f));
}

namespace {
// A waveform port, next to the number ones the rest of the file uses.
struct WaveSource {
    nodes::Out<std::vector<double>> out{"wave", [] { return std::vector<double>{0.5, -0.5, 0.25}; }};
    WidgetId out_label = 0;
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(WidgetTree::ViewBuilder& vb) { out_label = vb.port(out).node_ref.id; }
};

struct WaveSink {
    nodes::In<std::vector<double>> in{"in", std::vector<double>{}};
    WidgetId in_label = 0;
    std::vector<nodes::Port*> ports() { return {&in}; }
    void view(WidgetTree::ViewBuilder& vb) { in_label = vb.port(in).node_ref.id; }
};

// One board holding a waveform node and a number one, each with its own wire.
struct MixedBoard {
    WaveSource wave;
    WaveSink spectrum;
    Source number;
    Sink sink;
    nodes::Graph graph;
    nodes::NodeId w = graph.add(wave, {.title = "Wave", .family = "source", .at = P(20, 30)});
    nodes::NodeId s = graph.add(spectrum, {.title = "Spectrum", .family = "display", .at = P(380, 30)});
    nodes::NodeId n = graph.add(number, {.title = "Number", .family = "source", .at = P(20, 300)});
    nodes::NodeId k = graph.add(sink, {.title = "Sink", .family = "math", .at = P(380, 300)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

using MixedFixture = BasicFixture<MixedBoard>;
}

TEST_CASE("a wire is drawn in its type's colour, and a waveform is the thicker one") {
    MixedFixture f;
    REQUIRE(f.m.graph.connect({f.m.w, 0}, {f.m.s, 0}).has_value());
    REQUIRE(f.m.graph.connect({f.m.n, 0}, {f.m.k, 0}).has_value());
    f.frame();
    const Theme& t = f.tree.theme();
    const auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 2);

    // The slots are the registry's: the first is the waveform, the second the number.
    CHECK(nodes::wire_slot(f.m.wave.out.type()) == 0);
    CHECK(nodes::wire_slot(f.m.number.out.type()) == 1);
    const Color wave = t.node_wires[0], number = t.node_wires[1];

    const auto drawn_wave = leaving(wires, dot_of(f, f.m.wave.out_label, true));
    REQUIRE(drawn_wave);
    CHECK(drawn_wave->color == wave);
    CHECK(drawn_wave->thickness == doctest::Approx(3.f));

    const auto drawn_number = leaving(wires, dot_of(f, f.m.number.out_label, true));
    REQUIRE(drawn_number);
    CHECK(drawn_number->color == number);
    CHECK(drawn_number->thickness == doctest::Approx(2.f));
}

TEST_CASE("a port's dot is a rounded square in its type's colour, in a ring of the board's") {
    MixedFixture f;
    const Theme& t = f.tree.theme();
    const Point at = dot_of(f, f.m.wave.out_label, true);
    const Color wave = t.node_wires[nodes::wire_slot(f.m.wave.out.type())];
    const auto here = centred_on(f.all<RoundedRect>(), at);

    const auto face = std::ranges::find_if(here, [&](const RoundedRect& r) {
        return r.color == wave && r.thickness == 0.f;
    });
    REQUIRE(face != here.end());
    CHECK(face->rect.extent.w.raw() == doctest::Approx(12.f));
    CHECK(face->rect.extent.h.raw() == doctest::Approx(12.f));
    CHECK(face->radius == doctest::Approx(3.f));

    const auto ring = std::ranges::find_if(here, [&](const RoundedRect& r) {
        return r.color == t.node_board_bg && r.thickness == 0.f;
    });
    REQUIRE(ring != here.end());
    CHECK(ring->rect.extent.w.raw() == doctest::Approx(18.f));   // 3 px of board on every side
    CHECK(ring->radius == doctest::Approx(6.f));
}

TEST_CASE("while a wire is dragged, a port that cannot take it fades, and the trail takes its colour") {
    MixedFixture f;
    const Theme& t = f.tree.theme();
    const Point from = dot_of(f, f.m.wave.out_label, true);
    const Point takes_it = dot_of(f, f.m.spectrum.in_label, false);
    const Point refuses = dot_of(f, f.m.sink.in_label, false);
    f.press(from);
    f.move_to(P((from.x.raw() + takes_it.x.raw()) / 2, (from.y.raw() + takes_it.y.raw()) / 2));

    const Color wave = t.node_wires[nodes::wire_slot(f.m.wave.out.type())];
    const Color number = t.node_wires[nodes::wire_slot(f.m.number.out.type())];

    // The wire following the pointer is the dragged type's too.
    std::vector<Polyline> trails;
    for (auto& c : f.snap->overlay.commands)
        if (auto* p = std::get_if<Polyline>(&c)) trails.push_back(*p);
    REQUIRE(trails.size() == 1);
    CHECK(trails[0].color == wave);
    CHECK(trails[0].thickness == doctest::Approx(3.f));

    // A port taking it is drawn as ever; one that cannot is drawn at 35 %.
    const auto compatible = centred_on(f.all<RoundedRect>(), takes_it);
    CHECK(std::ranges::any_of(compatible, [&](const RoundedRect& r) { return r.color == wave; }));

    const auto incompatible = centred_on(f.all<RoundedRect>(), refuses);
    CHECK(std::ranges::any_of(incompatible, [&](const RoundedRect& r) {
        return r.color == faded(number, 0.35f);
    }));
    CHECK(std::ranges::none_of(incompatible, [&](const RoundedRect& r) { return r.color == number; }));
}

namespace {

// A number source whose output follows a level the test moves.
struct Dial {
    Field<double> level{0.25};
    nodes::Out<double> out{"out", [this] { return level.get(); }};
    WidgetId out_label = 0;
    std::vector<nodes::Port*> ports() { return {&out}; }
    void view(WidgetTree::ViewBuilder& vb) { out_label = vb.port(out).node_ref.id; }
};

struct Probed {
    Dial level;
    Sink sink;
    nodes::Graph graph;
    nodes::NodeId a = graph.add(level, {.title = "Level", .family = "source", .at = P(20, 30)});
    nodes::NodeId b = graph.add(sink, {.title = "Sink", .family = "math", .at = P(300, 60)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

using ProbedFixture = BasicFixture<Probed>;

// The pointer moves with no button held, as it does when it only rests on the board.
void hover(ProbedFixture& f, Point at) {
    route_event(f.tree, f.snap.get(), MouseMove{at, 0});
    f.frame();
}

Point on_the_wire(const ProbedFixture& f) {
    const auto wires = f.all<Polyline>();
    return wires.back().points[12];
}

// The bubble: its text drawn in the theme's probe colour.
std::optional<TextCmd> bubble_text(const ProbedFixture& f) {
    const Color c = f.tree.theme().node_probe_text;
    for (const auto& t : f.all<TextCmd>())
        if (t.color == c) return t;
    return std::nullopt;
}

std::string shown_value(ProbedFixture& f) {
    return nodes::format_value(f.m.level.out.type(), f.m.level.out.value());
}
}

TEST_CASE("hovering a wire shows its value in a bubble, in the theme's probe look") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    CHECK_FALSE(bubble_text(f));
    const Point at = on_the_wire(f);
    hover(f, at);
    const auto text = bubble_text(f);
    REQUIRE(text);
    CHECK(text->text == shown_value(f));
    CHECK(text->font == FontStyle{FontRole::Mono});
    CHECK(text->size == doctest::Approx(11.f));

    const Theme& t = f.tree.theme();
    const auto boxes = f.all<RoundedRect>();
    const auto box = *std::ranges::find_if(boxes, [&](const RoundedRect& r) {
        return r.color == t.node_probe_bg && r.thickness == 0.f;
    });
    CHECK(box.radius == doctest::Approx(10.f));
    CHECK(box.rect.origin.x.raw() == doctest::Approx(at.x.raw() + 12.f));
    CHECK(box.rect.origin.y.raw() + box.rect.extent.h.raw() == doctest::Approx(at.y.raw() - 12.f));
    const Color wire = t.node_wires[nodes::wire_slot(f.m.level.out.type())];
    CHECK(std::ranges::any_of(boxes, [&](const RoundedRect& r) {
        return r.color == wire && r.thickness == doctest::Approx(1.f) && r.rect.origin == box.rect.origin;
    }));

    const auto wires = f.all<Polyline>();   // the hovered wire carries the under-stroke
    REQUIRE(wires.size() == 2);
    CHECK(wires[0].thickness == doctest::Approx(9.f));

    hover(f, empty_spot);
    CHECK_FALSE(bubble_text(f));
    CHECK(f.all<Polyline>().size() == 1);
}

TEST_CASE("the bubble follows the output while the pointer rests") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    hover(f, on_the_wire(f));
    const std::string before = bubble_text(f)->text;
    f.m.level.level.set(0.75);
    f.frame();
    REQUIRE(bubble_text(f));
    CHECK(bubble_text(f)->text == shown_value(f));
    CHECK(bubble_text(f)->text != before);
}

TEST_CASE("removing the probed wire's source node clears the probe") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    hover(f, on_the_wire(f));
    REQUIRE(bubble_text(f));
    f.m.graph.remove(f.m.a);
    f.frame();
    CHECK_FALSE(bubble_text(f));
    f.m.level.level.set(0.5);   // nothing is listening any more
    f.frame();
    CHECK_FALSE(bubble_text(f));
    CHECK(f.all<Polyline>().empty());
}

TEST_CASE("disconnecting the probed wire clears the probe") {
    ProbedFixture f;
    const auto e = f.m.graph.connect({f.m.a, 0}, {f.m.b, 0});
    REQUIRE(e.has_value());
    f.frame();
    hover(f, on_the_wire(f));
    REQUIRE(bubble_text(f));
    f.m.graph.disconnect(*e);
    f.frame();
    CHECK_FALSE(bubble_text(f));
}

TEST_CASE("hovering a card, or pressing a button, shows no probe") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    const Point at = on_the_wire(f);
    hover(f, at);
    REQUIRE(bubble_text(f));
    hover(f, *f.text_at("Sink") + Offset{DX{4.f}, DY{4.f}});
    CHECK_FALSE(bubble_text(f));
    route_event(f.tree, f.snap.get(), MouseMove{at, 1});   // a button held: a drag, not a hover
    f.frame();
    CHECK_FALSE(bubble_text(f));
}

TEST_CASE("an output changing under a hidden probe does not redraw the board") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    hover(f, on_the_wire(f));
    hover(f, *f.text_at("Sink") + Offset{DX{4.f}, DY{4.f}});   // a card took the hover: the probe is hidden
    REQUIRE_FALSE(bubble_text(f));
    f.tree.clear_dirty();
    REQUIRE_FALSE(f.tree.any_dirty());
    f.m.level.level.set(0.75);
    CHECK_FALSE(f.tree.any_dirty());
}

TEST_CASE("leaving the window clears the probe") {
    ProbedFixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    hover(f, on_the_wire(f));
    REQUIRE(bubble_text(f));
    route_event(f.tree, f.snap.get(), MouseLeave{});
    f.frame();
    CHECK_FALSE(bubble_text(f));
}

TEST_CASE("at zoom 0.5 the bubble keeps its natural size") {
    ProbedFixture f;
    at_zoom(f, 0.5f);
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    hover(f, on_the_wire(f));
    const auto text = bubble_text(f);
    REQUIRE(text);
    CHECK(text->size == doctest::Approx(11.f));
    const auto boxes = f.all<RoundedRect>();
    const auto box = std::ranges::find_if(boxes, [&](const RoundedRect& r) {
        return r.color == f.tree.theme().node_probe_bg && r.thickness == 0.f;
    });
    REQUIRE(box != boxes.end());
    CHECK(box->rect.extent.h.raw() == doctest::Approx(11.f * 1.4f + 6.f));   // 3 px of padding
}

namespace {

struct Amp {   // a node a palette makes: no view, its ports shown as they are
    nodes::In<double> in{"in", 0.0};
    nodes::Out<double> out{"amp out", [this] { return in.get(); }};
    std::vector<nodes::Port*> ports() { return {&in, &out}; }
};

struct Workbench {
    nodes::Graph graph;
    std::vector<nodes::NodeKind> kinds{nodes::kind_of<Amp>("Amplifier", "math"), nodes::kind_of<Amp>("Mixer", "sink")};
    WidgetId palette = 0, board = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            palette = vb.node_palette(graph, kinds);
            board = vb.node_board(graph);
        });
    }
};

using PaletteFixture = BasicFixture<Workbench>;

// What the palette row titled `title` draws: its text sits inside it.
Point row_of(const PaletteFixture& f, std::string_view title) {
    const auto t = f.text_cmd(title);
    REQUIRE(t);
    return t->origin + Offset{DX{2.f}, DY{2.f}};
}

void click_twice(PaletteFixture& f, Point at) {
    for (int i = 0; i < 2; ++i) {
        route_event(f.tree, f.snap.get(), MouseButton{at, buttons::left, true, 0, static_cast<std::uint8_t>(i + 1)});
        route_event(f.tree, f.snap.get(), MouseButton{at, buttons::left, false, 0, static_cast<std::uint8_t>(i + 1)});
        f.frame();
    }
}

Point on_board(const PaletteFixture& f, float x, float y) {
    return laid_out(f, f.m.board).origin + Offset{DX{x}, DY{y}};
}

}

TEST_CASE("a palette row shows its family's colour and its title") {
    PaletteFixture f;
    CHECK(f.text_cmd("Amplifier"));
    const auto& theme = f.tree.theme();
    bool found = false;
    for (WidgetId id : f.tree.leaf_ids())
        if (f.tree.within(id, f.m.palette))
            for (const auto& c : f.draws_of<Circle>(id))
                found = found || c.color == node_family_color(theme, "math");
    CHECK(found);
}

TEST_CASE("dragging a palette row onto the board adds a node; at zoom 0.5 its card's top-left is under the drop") {
    PaletteFixture f;
    at_zoom(f, 0.5f);
    const Point drop = on_board(f, 200.f, 100.f);
    f.drag(row_of(f, "Amplifier"), drop);
    REQUIRE(f.m.graph.nodes().size() == 1);
    const auto id = f.m.graph.nodes().front();
    CHECK(f.m.graph.position(id).x.raw() == doctest::Approx(400));
    CHECK(f.m.graph.position(id).y.raw() == doctest::Approx(200));
    CHECK(f.m.graph.options(id).title == "Amplifier");
    CHECK(f.m.graph.options(id).family == "math");
    CHECK(f.m.graph.options(id).removable);
    for (WidgetId leaf : f.tree.leaf_ids())
        if (f.tree.within(leaf, f.m.board) && !f.draws_of<TextCmd>(leaf).empty()
            && f.draws_of<TextCmd>(leaf).front().text == "amp out") {
            const Rect card = laid_out(f, f.tree.parent_of(*f.tree.parent_of(leaf)).value());
            CHECK(card.origin.x.raw() == doctest::Approx(drop.x.raw()));
            CHECK(card.origin.y.raw() == doctest::Approx(drop.y.raw()));
        }
}

TEST_CASE("a palette row lands where the board is panned to, and dropped off the board adds nothing") {
    PaletteFixture f;
    f.m.graph.set_view({.pan = P(-30, -10), .zoom = 1.f});
    f.frame();
    f.drag(row_of(f, "Mixer"), on_board(f, 100.f, 50.f));
    REQUIRE(f.m.graph.nodes().size() == 1);
    CHECK(f.m.graph.position(f.m.graph.nodes().front()) == P(130, 60));
    f.drag(row_of(f, "Mixer"), P(1.f, 590.f));
    CHECK(f.m.graph.nodes().size() == 1);
}

TEST_CASE("activating a palette row twice adds two nodes 24 units apart, from the board's visible top-left") {
    PaletteFixture f;
    f.m.graph.set_view({.pan = P(-30, -10), .zoom = 1.f});
    f.frame();
    click_twice(f, row_of(f, "Amplifier"));
    REQUIRE(f.m.graph.nodes().size() == 1);
    click_twice(f, row_of(f, "Amplifier"));
    REQUIRE(f.m.graph.nodes().size() == 2);
    const auto ids = f.m.graph.nodes();
    CHECK(f.m.graph.position(ids[0]) == P(30, 10));
    CHECK(f.m.graph.position(ids[1]) == P(54, 34));
}

TEST_CASE("a single click on a palette row adds nothing; Enter on the focused row adds one") {
    PaletteFixture f;
    const Point at = row_of(f, "Amplifier");
    route_event(f.tree, f.snap.get(), MouseButton{at, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseButton{at, buttons::left, false});
    f.frame();
    CHECK(f.m.graph.nodes().empty());
    key(f, keys::enter);
    CHECK(f.m.graph.nodes().size() == 1);
}

TEST_CASE("a node made by the palette is owned by the graph: removing it drops its wires") {
    PaletteFixture f;
    click_twice(f, row_of(f, "Amplifier"));
    click_twice(f, row_of(f, "Mixer"));
    const auto ids = f.m.graph.nodes();
    REQUIRE(ids.size() == 2);
    REQUIRE(f.m.graph.connect({ids[0], 1}, {ids[1], 0}));
    f.m.graph.remove(ids[0]);
    CHECK(f.m.graph.edges().empty());
    CHECK(f.m.graph.nodes() == std::vector{ids[1]});
}

TEST_CASE("an owned node deleted from the board leaves nothing of its ports for a paint or a hover to read") {
    PaletteFixture f;
    f.drag(row_of(f, "Amplifier"), on_board(f, 300.f, 100.f));
    REQUIRE(f.m.graph.nodes().size() == 1);
    Point header{};
    for (const auto& t : f.all<TextCmd>())
        if (t.text == "Amplifier" && t.origin.x.raw() > laid_out(f, f.m.board).origin.x.raw()) header = t.origin;
    f.press(header + Offset{DX{2.f}, DY{2.f}});
    f.release(header + Offset{DX{2.f}, DY{2.f}});
    key(f, keys::delete_);
    CHECK(f.m.graph.nodes().empty());
    f.move_to(on_board(f, 300.f, 110.f));
    f.move_to(on_board(f, 305.f, 115.f));
}

TEST_CASE("an owned node removed by the app is gone from the board's hit and paint state before the next frame") {
    PaletteFixture f;
    f.drag(row_of(f, "Amplifier"), on_board(f, 300.f, 100.f));
    REQUIRE(f.m.graph.nodes().size() == 1);
    f.m.graph.remove(f.m.graph.nodes().front());
    f.move_to(on_board(f, 300.f, 110.f));
    f.press(on_board(f, 300.f, 110.f));
    f.release(on_board(f, 300.f, 110.f));
    CHECK(f.m.graph.nodes().empty());
}

namespace {
constexpr const char* long_title = "A rather long block title for a narrow palette, longer than any fixed row";
struct LongWorkbench : Workbench {
    LongWorkbench() { kinds = {nodes::kind_of<Amp>(long_title, "math")}; }
};
}

TEST_CASE("a palette row is at least as wide as its title") {
    BasicFixture<LongWorkbench> f;
    const auto title = f.text_cmd(long_title);
    REQUIRE(title);
    const float text_w = ui::text_width(f.tree.theme().measurer, long_title, title->size);
    bool wide_enough = false;
    for (WidgetId id : f.tree.leaf_ids())
        if (f.tree.within(id, f.m.palette)) wide_enough = laid_out(f, id).extent.w.raw() >= text_w;
    CHECK(wide_enough);
}

namespace {

// The lines drawn in the snap guides' colour: the theme's accent at 60 %.
std::vector<Line> guides_drawn(const Fixture& f) {
    const Color guide = faded(f.tree.theme().accent, 0.6f);
    std::vector<Line> out;
    for (const auto& l : f.all<Line>())
        if (l.color == guide) out.push_back(l);
    return out;
}

Point grab_title(const Fixture& f, std::string_view title) { return *f.text_at(title) + Offset{DX{4.f}, DY{4.f}}; }

void alt_press(Fixture& f, Point at) {
    route_event(f.tree, f.snap.get(), MouseButton{at, buttons::left, true, mods::alt});
    f.frame();
}

}

TEST_CASE("a header dragged 4 px from a neighbour's edge snaps to it and shows one guide, none after the release") {
    Fixture f;
    park_sink(f, P(300, 400));
    const Point grab = grab_title(f, "Source");
    f.press(grab);
    f.move_to(grab + Offset{DX{276.f}, DY{0.f}});   // the card's left edge 4 px short of the sink's
    CHECK(f.m.graph.position(f.m.a) == P(300, 30));
    REQUIRE(guides_drawn(f).size() == 1);
    CHECK(guides_drawn(f)[0].from.x.raw() == doctest::Approx(300));
    CHECK(guides_drawn(f)[0].to.x.raw() == doctest::Approx(300));
    CHECK(guides_drawn(f)[0].to.y.raw() > guides_drawn(f)[0].from.y.raw());   // a vertical line spanning both cards
    f.release(grab + Offset{DX{276.f}, DY{0.f}});
    CHECK(guides_drawn(f).empty());
}

TEST_CASE("a node follows the pointer again once it is dragged past the snap") {
    Fixture f;
    park_sink(f, P(300, 400));
    const Point grab = grab_title(f, "Source");
    f.press(grab);
    f.move_to(grab + Offset{DX{276.f}, DY{0.f}});
    REQUIRE(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(300));
    f.move_to(grab + Offset{DX{288.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(308));
    f.release(grab + Offset{DX{288.f}, DY{0.f}});
}

TEST_CASE("at zoom 0.25 5 screen px snap and 7 do not") {
    Fixture f;
    const float source_w = f.tree.arranged_extent(f.card_of(f.m.source.out_label))->w.raw();
    park_sink(f, P(445.2f, 400));   // its left edge 20 graph units right of the source's right edge, once dragged to x = 280
    REQUIRE(445.2f - 280.f - source_w == doctest::Approx(20.f));
    at_zoom(f, 0.25f);
    const Point grab = grab_title(f, "Source");
    f.press(grab);
    f.move_to(grab + Offset{DX{65.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(300));
    REQUIRE(!guides_drawn(f).empty());
    CHECK(guides_drawn(f)[0].from.x.raw() == doctest::Approx(445.2f * 0.25f));
    f.release(grab + Offset{DX{65.f}, DY{0.f}});
    f.m.graph.move(f.m.a, P(20, 30));
    f.frame();
    const Point again = grab_title(f, "Source");
    f.press(again);
    f.move_to(again + Offset{DX{63.f}, DY{0.f}});   // 28 graph units short
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(272));
    CHECK(guides_drawn(f).empty());
    f.release(again + Offset{DX{63.f}, DY{0.f}});
}

TEST_CASE("a wired input is levelled with the output feeding it") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    f.frame();
    const float out_from_top = middle_y(*f.row_of(f.m.source.out_label)) - f.allocated(f.card_of(f.m.source.out_label))->origin.y.raw();
    const float in_from_top = middle_y(*f.row_of(f.m.sink.in_label)) - f.allocated(f.card_of(f.m.sink.in_label))->origin.y.raw();
    const float level_y = 30.f + out_from_top - in_from_top;
    const Point grab = grab_title(f, "Sink");
    f.press(grab);
    f.move_to(grab + Offset{DX{0.f}, DY{level_y + 3.f - 60.f}});
    const auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].points.front().y.raw() == doctest::Approx(wires[0].points.back().y.raw()));
    CHECK(!guides_drawn(f).empty());
    f.release(grab);
}

TEST_CASE("with Alt held a header is dragged freely: no snap, no guide") {
    Fixture f;
    park_sink(f, P(300, 400));
    const Point grab = grab_title(f, "Source");
    alt_press(f, grab);
    f.move_to(grab + Offset{DX{276.f}, DY{0.f}});
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(296));
    CHECK(guides_drawn(f).empty());
    f.release(grab + Offset{DX{276.f}, DY{0.f}});
}

namespace {

using Clock = std::chrono::steady_clock;

// A fixture whose tree runs on a clock the test moves.
struct Gliding : Fixture {
    Clock::time_point at = Clock::now();
    Gliding() {
        tree.set_clock([this] { return at; });
        REQUIRE(m.graph.connect({m.a, 0}, {m.b, 0}).has_value());
        m.graph.move(m.a, P(300, 200));
        m.graph.move(m.b, P(20, 30));   // the wire runs backwards: tidy swaps them
        frame();
    }
    void after(int ms) { at += std::chrono::milliseconds(ms); frame(); }
    std::map<nodes::NodeId, Size> card_sizes() {
        std::map<nodes::NodeId, Size> sizes;
        sizes.emplace(m.a, laid_out(*this, card_of(m.source.out_label)).extent);
        sizes.emplace(m.b, laid_out(*this, card_of(m.sink.in_label)).extent);
        return sizes;
    }
};

} // namespace

TEST_CASE("tidy glides every node to its column over 200 ms") {
    Gliding f;
    const auto target = nodes::tidy_layout(f.m.graph, f.card_sizes());
    f.m.graph.request_tidy();
    f.frame();
    f.after(100);
    const float a_x = f.m.graph.position(f.m.a).x.raw();
    CHECK(a_x < 300.f);                               // on its way
    CHECK(a_x > target.at(f.m.a).x.raw());            // and not there yet
    f.after(150);
    f.after(1);
    for (const auto id : {f.m.a, f.m.b}) {
        CHECK(f.m.graph.position(id).x.raw() == doctest::Approx(target.at(id).x.raw()));
        CHECK(f.m.graph.position(id).y.raw() == doctest::Approx(target.at(id).y.raw()));
    }
    CHECK(f.m.graph.position(f.m.b).x.raw() > f.m.graph.position(f.m.a).x.raw());
}

TEST_CASE("a glide asks the tree for its next frame until it ends") {
    Gliding f;
    f.m.graph.request_tidy();
    f.frame();
    f.after(50);
    const auto wake = f.tree.next_wake();
    REQUIRE(wake);
    f.tree.tick(*wake);   // the app's timer: it consumes the deadline
    f.after(300);
    CHECK_FALSE(f.tree.next_wake());
}

TEST_CASE("a node grabbed during a glide stops gliding; the others arrive") {
    Gliding f;
    const auto target = nodes::tidy_layout(f.m.graph, f.card_sizes());
    f.m.graph.request_tidy();
    f.frame();
    f.after(60);
    auto title = f.text_at("Source");
    REQUIRE(title);
    const Point grab = *title + Offset{DX{2.f}, DY{2.f}};
    f.press(grab);
    f.move_to(grab + Offset{DX{10.f}, DY{0.f}});
    const Point held = f.m.graph.position(f.m.a);
    f.after(300);
    f.after(1);
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(held.x.raw()));
    CHECK(f.m.graph.position(f.m.b).x.raw() == doctest::Approx(target.at(f.m.b).x.raw()));
}

TEST_CASE("a node removed during a glide is skipped; the others arrive") {
    Gliding f;
    const auto target = nodes::tidy_layout(f.m.graph, f.card_sizes());
    f.m.graph.request_tidy();
    f.frame();
    f.after(60);
    f.m.graph.remove(f.m.b);
    f.frame();
    f.after(300);
    f.after(1);
    CHECK(f.m.graph.position(f.m.a).x.raw() == doctest::Approx(target.at(f.m.a).x.raw()));
}

TEST_CASE("a guide is drawn where a panned board shows its graph points") {
    Fixture f;
    park_sink(f, P(300, 400));
    f.m.graph.set_view({.pan = P(10, -20), .zoom = 1.f});
    f.frame();
    const Point grab = grab_title(f, "Source");
    f.press(grab);
    f.move_to(grab + Offset{DX{276.f}, DY{0.f}});
    REQUIRE(guides_drawn(f).size() == 1);
    CHECK(guides_drawn(f)[0].from.x.raw() == doctest::Approx(310));   // graph x 300 + pan 10
    CHECK(guides_drawn(f)[0].from.y.raw() == doctest::Approx(30 - 20));   // the source's top, + pan
    f.release(grab);
}

TEST_CASE("a wired output is levelled with the input it feeds, at zoom 1 and at zoom 0.5") {
    for (const float zoom : {1.f, 0.5f}) {
        Fixture f;
        REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
        at_zoom(f, zoom);
        const float out_from_top = (middle_y(*f.row_of(f.m.source.out_label)) - f.allocated(f.card_of(f.m.source.out_label))->origin.y.raw()) / zoom;
        const float in_from_top = (middle_y(*f.row_of(f.m.sink.in_label)) - f.allocated(f.card_of(f.m.sink.in_label))->origin.y.raw()) / zoom;
        const float level_y = 60.f + in_from_top - out_from_top;   // the source's top that levels it with the sink
        const Point grab = grab_title(f, "Source");
        f.press(grab);
        f.move_to(grab + Offset{DX{0.f}, DY{(level_y + 3.f - 30.f) * zoom}});
        const auto wires = f.all<Polyline>();
        REQUIRE(wires.size() == 1);
        CHECK(wires[0].points.front().y.raw() == doctest::Approx(wires[0].points.back().y.raw()));
        f.release(grab);
    }
}

TEST_CASE("a wired input is levelled at zoom 0.5 too") {
    Fixture f;
    REQUIRE(f.m.graph.connect({f.m.a, 0}, {f.m.b, 0}).has_value());
    at_zoom(f, 0.5f);
    const float out_from_top = (middle_y(*f.row_of(f.m.source.out_label)) - f.allocated(f.card_of(f.m.source.out_label))->origin.y.raw()) / 0.5f;
    const float in_from_top = (middle_y(*f.row_of(f.m.sink.in_label)) - f.allocated(f.card_of(f.m.sink.in_label))->origin.y.raw()) / 0.5f;
    const float level_y = 30.f + out_from_top - in_from_top;
    const Point grab = grab_title(f, "Sink");
    f.press(grab);
    f.move_to(grab + Offset{DX{0.f}, DY{(level_y + 3.f - 60.f) * 0.5f}});
    const auto wires = f.all<Polyline>();
    REQUIRE(wires.size() == 1);
    CHECK(wires[0].points.front().y.raw() == doctest::Approx(wires[0].points.back().y.raw()));
    f.release(grab);
}

namespace {

bool overlap(Rect a, Rect b) {
    return a.origin.x.raw() < right(b) && b.origin.x.raw() < right(a)
        && a.origin.y.raw() < b.origin.y.raw() + b.extent.h.raw()
        && b.origin.y.raw() < a.origin.y.raw() + a.extent.h.raw();
}

struct Growing : Editor {
    Sink more;
};

struct Cards {
    WidgetTree& tree;
    std::vector<Rect> of(std::initializer_list<WidgetId> labels) const {
        std::vector<Rect> out;
        for (const WidgetId label : labels) out.push_back(*tree.laid_out_at(*tree.parent_of(*tree.parent_of(label))));
        return out;
    }
};

} // namespace

TEST_CASE("a tidy asked in the turn a node is added waits for its card: no cards overlap") {
    Growing m;
    WidgetTree tree{m};
    Clock::time_point at = Clock::now();
    tree.set_clock([&] { return at; });
    std::uint64_t v = 1;
    auto snap = tree.build_snapshot(800, 600, v);
    m.graph.add(m.more, {.title = "More", .family = "math", .at = P(20, 30)});
    m.graph.request_tidy();
    for (int i = 0; i < 20; ++i) {
        at += std::chrono::milliseconds(30);
        snap = tree.build_snapshot(800, 600, ++v);
    }
    const auto cards = Cards{tree}.of({m.source.out_label, m.sink.in_label, m.more.in_label});
    for (std::size_t i = 0; i < cards.size(); ++i)
        for (std::size_t j = i + 1; j < cards.size(); ++j) CHECK_FALSE(overlap(cards[i], cards[j]));
}

TEST_CASE("a tidy asked before the first frame waits for the sizes to settle: no cards overlap") {
    Headed m;
    m.graph.request_tidy();
    WidgetTree tree{m};
    Clock::time_point at = Clock::now();
    tree.set_clock([&] { return at; });
    for (std::uint64_t v = 1; v <= 20; ++v) {
        at += std::chrono::milliseconds(30);
        auto snap = tree.build_snapshot(240, 400, v);
        static_cast<void>(snap);
    }
    const auto cards = Cards{tree}.of({m.source.out_label, m.sink.in_label});
    CHECK_FALSE(overlap(cards[0], cards[1]));
}

TEST_CASE("a fit asked with a tidy shows the cards where the tidy leaves them") {
    Gliding f;
    f.m.graph.request_tidy();
    f.m.graph.request_fit();
    for (int i = 0; i < 20; ++i) f.after(30);
    CHECK_FALSE(f.m.graph.fit_requested());
    const Rect board = laid_out(f, *f.tree.parent_of(f.card_of(f.m.source.out_label)));
    for (const WidgetId label : {f.m.source.out_label, f.m.sink.in_label}) {
        const Rect r = laid_out(f, f.card_of(label));
        CHECK(board.contains(r.origin));
        CHECK(board.contains(P(right(r) - 1, bottom(r) - 1)));
    }
}
