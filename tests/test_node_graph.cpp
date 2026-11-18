#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/graph.hpp>

using namespace prism::nodes;
using prism::core::Point;

namespace {

struct Source {
    In<double> v{"v", 0.0};
    Out<double> out{"out", [this] { return v.get(); }};
    std::vector<Port*> ports() { return {&v, &out}; }
};

struct Text {
    Out<std::string> out{"out", [] { return std::string{"x"}; }};
    std::vector<Port*> ports() { return {&out}; }
};

struct Sink {
    In<double> in{"in", -1.0};
    std::vector<Port*> ports() { return {&in}; }
};

constexpr std::size_t OUT = 1;

NodeOptions titled(std::string t = {}) { NodeOptions o; o.title = std::move(t); return o; }

}

TEST_CASE("a wire carries the output's value now and on each change") {
    Graph g;
    Source s; Sink k;
    s.v.field.set(3.0);
    const auto a = g.add(s, titled("s"));
    const auto b = g.add(k, titled("k"));
    REQUIRE(g.connect({a, OUT}, {b, 0}).has_value());
    CHECK(k.in.get() == 3.0);
    s.v.field.set(5.0);
    CHECK(k.in.get() == 5.0);
}

TEST_CASE("connect takes the ends in either order") {
    Graph g;
    Source s; Sink k;
    const auto a = g.add(s, titled("s"));
    const auto b = g.add(k, titled("k"));
    CHECK(g.can_connect({b, 0}, {a, OUT}));
    const auto id = g.connect({b, 0}, {a, OUT});
    REQUIRE(id.has_value());
    CHECK(g.edge(*id)->from == PortRef{a, OUT});
    CHECK(g.edge(*id)->to == PortRef{b, 0});
}

TEST_CASE("an output feeds several inputs; a second wire into an input replaces the first") {
    Graph g;
    Source s1, s2; Sink k1, k2;
    const auto a = g.add(s1, titled()), b = g.add(s2, titled()), c = g.add(k1, titled()), d = g.add(k2, titled());
    const auto e1 = g.connect({a, OUT}, {c, 0});
    REQUIRE(g.connect({a, OUT}, {d, 0}));
    REQUIRE(g.connect({b, OUT}, {c, 0}));
    CHECK(g.edges().size() == 2);
    CHECK(g.edge(*e1) == nullptr);
    CHECK(g.edge_into({c, 0}).has_value());
}

TEST_CASE("a wire between different types is refused") {
    Graph g;
    Text t; Sink k;
    const auto a = g.add(t, titled()), b = g.add(k, titled());
    const auto r = g.connect({a, 0}, {b, 0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == ConnectError::TypeMismatch);
    CHECK(g.edges().empty());
}

TEST_CASE("two outputs or two inputs cannot be wired") {
    Graph g;
    Source s1, s2; Sink k1, k2;
    const auto a = g.add(s1, titled()), b = g.add(s2, titled()), c = g.add(k1, titled()), d = g.add(k2, titled());
    CHECK(g.connect({a, OUT}, {b, OUT}).error() == ConnectError::NotOutputToInput);
    CHECK(g.connect({c, 0}, {d, 0}).error() == ConnectError::NotOutputToInput);
    CHECK(g.connect({a, 7}, {d, 0}).error() == ConnectError::UnknownPort);
}

TEST_CASE("a wire that closes a loop is refused, even through other nodes") {
    Graph g;
    Source a, b;
    const auto ia = g.add(a, titled()), ib = g.add(b, titled());
    REQUIRE(g.connect({ia, OUT}, {ib, 0}));
    CHECK(g.connect({ib, OUT}, {ia, 0}).error() == ConnectError::Cycle);
    CHECK(g.connect({ia, OUT}, {ia, 0}).error() == ConnectError::Cycle);
    CHECK(g.edges().size() == 1);
}

TEST_CASE("disconnecting reverts the input to its default") {
    Graph g;
    Source s; Sink k;
    s.v.field.set(4.0);
    const auto id = g.connect({g.add(s, titled()), OUT}, {g.add(k, titled()), 0});
    CHECK(k.in.get() == 4.0);
    g.disconnect(*id);
    CHECK(k.in.get() == -1.0);
    s.v.field.set(9.0);
    CHECK(k.in.get() == -1.0);
}

TEST_CASE("removing a node removes its wires and reverts the inputs they fed") {
    Graph g;
    Source s; Sink k;
    const auto a = g.add(s, titled()), b = g.add(k, titled());
    REQUIRE(g.connect({a, OUT}, {b, 0}));
    s.v.field.set(4.0);
    CHECK(k.in.get() == 4.0);
    g.remove(a);
    CHECK(g.edges().empty());
    CHECK(g.nodes() == std::vector<NodeId>{b});
    CHECK(k.in.get() == -1.0);
    s.v.field.set(6.0);
    CHECK(k.in.get() == -1.0);
}

TEST_CASE("destroying the graph first leaves the nodes usable") {
    Source s; Sink k;
    {
        Graph g;
        const auto a = g.add(s, titled()), b = g.add(k, titled());
        REQUIRE(g.connect({a, OUT}, {b, 0}));
    }
    s.v.field.set(2.0);
    CHECK(k.in.get() == 0.0);
}

TEST_CASE("structure changes on add, connect, disconnect and remove; layout on move") {
    Graph g;
    Source s; Sink k;
    auto n = g.structure.get();
    const auto a = g.add(s, titled()); CHECK(g.structure.get() > n); n = g.structure.get();
    const auto b = g.add(k, titled()); n = g.structure.get();
    const auto e = g.connect({a, OUT}, {b, 0}); CHECK(g.structure.get() > n); n = g.structure.get();
    g.disconnect(*e); CHECK(g.structure.get() > n); n = g.structure.get();
    g.remove(a); CHECK(g.structure.get() > n);
    const auto l = g.layout.get();
    g.move(b, Point{prism::core::X{3}, prism::core::Y{4}});
    CHECK(g.layout.get() > l);
    CHECK(g.position(b) == Point{prism::core::X{3}, prism::core::Y{4}});
}

TEST_CASE("panning shifts the board and asks for a layout, unless it is no move") {
    Graph g;
    CHECK(g.view().pan == Point{});
    const auto l = g.layout.get();
    g.pan_by(prism::core::DX{0}, prism::core::DY{0});
    CHECK(g.layout.get() == l);
    g.pan_by(prism::core::DX{5}, prism::core::DY{-7});
    CHECK(g.layout.get() > l);
    CHECK(g.view().pan == Point{prism::core::X{5}, prism::core::Y{-7}});
}

TEST_CASE("setting the view clamps the zoom and asks for a layout only on a change") {
    Graph g;
    const auto l = g.layout.get();
    g.set_view({Point{prism::core::X{5}, prism::core::Y{5}}, 9.f});
    CHECK(g.view().zoom == 2.f);
    CHECK(g.layout.get() == l + 1);
    g.set_view({Point{prism::core::X{5}, prism::core::Y{5}}, 9.f});
    CHECK(g.layout.get() == l + 1);
}

TEST_CASE("panning adds to the view's pan in board units") {
    Graph g;
    g.pan_by(prism::core::DX{10}, prism::core::DY{0});
    CHECK(g.view().pan.x.raw() == 10.f);
}

TEST_CASE("disconnecting or removing something unknown changes nothing") {
    Graph g;
    const auto n = g.structure.get();
    g.disconnect(42);
    g.remove(42);
    CHECK(g.structure.get() == n);
}

TEST_CASE("nodes that die before the graph leave it safe to destroy") {
    Graph g;
    {
        Source s; Sink k;
        const auto a = g.add(s, titled()), b = g.add(k, titled());
        REQUIRE(g.connect({a, OUT}, {b, 0}));
    }
}

TEST_CASE("a diamond of nodes is walked without revisits and still refuses its loop") {
    Graph g;
    Source a, b, c, d;
    const auto ia = g.add(a, titled()), ib = g.add(b, titled()), ic = g.add(c, titled()), id = g.add(d, titled());
    REQUIRE(g.connect({ia, OUT}, {ib, 0}));
    REQUIRE(g.connect({ia, OUT}, {ic, 0}));
    REQUIRE(g.connect({ib, OUT}, {id, 0}));
    REQUIRE(g.connect({ic, OUT}, {id, 0}));
    CHECK(g.connect({id, OUT}, {ia, 0}).error() == ConnectError::Cycle);
}

TEST_CASE("a fit is asked for, then made: every card shows in the board") {
    using prism::core::Rect; using prism::core::Size; using prism::core::X; using prism::core::Y;
    using prism::core::Width; using prism::core::Height;
    Graph g;
    CHECK_FALSE(g.fit_requested());
    const auto l = g.layout.get();
    g.request_fit();
    CHECK(g.fit_requested());
    CHECK(g.layout.get() == l + 1);   // the board lays out again, and fits there
    const Rect cards[] = {{Point{X{0}, Y{0}}, Size{Width{100}, Height{50}}},
                          {Point{X{900}, Y{0}}, Size{Width{100}, Height{50}}}};
    const Size board{Width{548}, Height{400}};
    g.fit(cards, board);
    CHECK_FALSE(g.fit_requested());
    CHECK(g.view() == fit_all(cards, board));
    CHECK(g.view().zoom == doctest::Approx(0.5f));
}
