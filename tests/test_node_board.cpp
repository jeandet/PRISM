// vb.node_board: a graph shown as cards on a board, its wires drawn from card edge to card edge.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>
#include <prism/nodes/graph.hpp>

#include <algorithm>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
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
    nodes::NodeId b = graph.add(sink, {.title = "Sink", .family = "math", .at = P(300, 60)});
    void view(WidgetTree::ViewBuilder& vb) { vb.node_board(graph); }
};

struct Fixture {
    Editor m;
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
