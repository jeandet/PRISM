#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/tidy.hpp>

#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

using namespace prism::core;
using namespace prism::nodes;
using doctest::Approx;

namespace {

constexpr std::size_t in_port = 0;
constexpr std::size_t out_port = 1;
constexpr float CARD_W = 160.f;
constexpr float CARD_H = 100.f;

struct Pass {
    In<double> in{"in", 0.0};
    Out<double> out{"out", [this] { return in.get(); }};
    std::vector<Port*> ports() { return {&in, &out}; }
};

// A card with two inputs, for a graph two wires share a sink through.
struct Mix {
    In<double> a{"a", 0.0};
    In<double> b{"b", 0.0};
    Out<double> out{"out", [this] { return a.get() + b.get(); }};
    std::vector<Port*> ports() { return {&a, &b, &out}; }
};

constexpr std::size_t MIX_A = 0;
constexpr std::size_t MIX_B = 1;

Point pt(float x, float y) { return Point{X{x}, Y{y}}; }
Size card(float w = CARD_W, float h = CARD_H) { return Size{Width{w}, Height{h}}; }

// A board of same-sized cards, wired as each test says.
struct Board {
    Graph graph;
    std::map<NodeId, Size> sizes;

    NodeId node(std::string title, float x, float y) {
        const NodeOptions options{.title = std::move(title), .family = {}, .at = pt(x, y),
                                  .removable = false};
        const NodeId id = graph.emplace<Pass>(options);
        sizes[id] = card();
        return id;
    }

    NodeId mix(std::string title, float x, float y) {
        const NodeOptions options{.title = std::move(title), .family = {}, .at = pt(x, y),
                                  .removable = false};
        const NodeId id = graph.emplace<Mix>(options);
        sizes[id] = card();
        return id;
    }

    void wire(NodeId from, NodeId to) { wire_into(from, out_port, to, in_port); }

    void wire_into(NodeId from, std::size_t out, NodeId to, std::size_t in) {
        REQUIRE(graph.connect({from, out}, {to, in}).has_value());
    }

    std::map<NodeId, Point> tidy(TidySpacing spacing = {}) const {
        return tidy_layout(graph, sizes, spacing);
    }
};

float x_of(const std::map<NodeId, Point>& laid, NodeId id) { return laid.at(id).x.raw(); }
float y_of(const std::map<NodeId, Point>& laid, NodeId id) { return laid.at(id).y.raw(); }

}

TEST_CASE("a chain of three cards gets a column each") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 10, 10);
    const auto c = board.node("c", 20, 20);
    board.wire(a, b);
    board.wire(b, c);

    const auto laid = board.tidy();

    REQUIRE(laid.size() == 3);
    CHECK(x_of(laid, a) == Approx(0));
    CHECK(x_of(laid, b) == Approx(CARD_W + 80));
    CHECK(x_of(laid, c) == Approx(2 * (CARD_W + 80)));
    CHECK(y_of(laid, a) == Approx(0));
    CHECK(y_of(laid, b) == Approx(0));
    CHECK(y_of(laid, c) == Approx(0));
}

TEST_CASE("a column starts after the widest card of the one before it") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto wide = board.node("wide", 10, 10);
    const auto last = board.node("last", 20, 20);
    board.sizes[wide] = card(240.f);
    board.wire(a, wide);
    board.wire(wide, last);

    const auto laid = board.tidy();

    CHECK(x_of(laid, a) == Approx(0));
    CHECK(x_of(laid, wide) == Approx(CARD_W + 80));
    CHECK(x_of(laid, last) == Approx(CARD_W + 80 + 240 + 80));
}

TEST_CASE("a diamond puts its sink two columns after its source") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto left = board.node("left", 10, 0);
    const auto right = board.node("right", 10, 200);
    const auto sink = board.mix("sink", 20, 0);
    board.wire(a, left);
    board.wire(a, right);
    board.wire_into(left, out_port, sink, MIX_A);
    board.wire_into(right, out_port, sink, MIX_B);

    const auto laid = board.tidy();

    CHECK(x_of(laid, a) == Approx(0));
    CHECK(x_of(laid, left) == Approx(CARD_W + 80));
    CHECK(x_of(laid, right) == Approx(CARD_W + 80));
    CHECK(x_of(laid, sink) == Approx(2 * (CARD_W + 80)));
    CHECK(std::abs(y_of(laid, left) - y_of(laid, right)) == Approx(CARD_H + 40));
}

TEST_CASE("a wire skipping a column still leaves its sink two columns out") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 10, 0);
    const auto c = board.mix("c", 20, 0);
    board.wire(a, b);
    board.wire_into(b, out_port, c, MIX_A);
    board.wire_into(a, out_port, c, MIX_B);

    const auto laid = board.tidy();

    CHECK(x_of(laid, c) == Approx(2 * (CARD_W + 80)));
}

TEST_CASE("two wires that would cross end up ordered so they don't") {
    Board board;
    const auto top = board.node("top", 0, 0);
    const auto bottom = board.node("bottom", 0, 200);
    const auto low = board.node("low", 300, 0);
    const auto high = board.node("high", 300, 200);
    board.wire(top, high);
    board.wire(bottom, low);

    const auto laid = board.tidy();

    CHECK(x_of(laid, high) == Approx(x_of(laid, low)));
    CHECK(y_of(laid, high) < y_of(laid, low));
}

TEST_CASE("a card with no wires goes below the wired ones of the first column") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 10, 10);
    const auto lonely = board.node("lonely", 5, 300);
    board.wire(a, b);

    const auto laid = board.tidy();

    CHECK(x_of(laid, lonely) == Approx(x_of(laid, a)));
    CHECK(y_of(laid, lonely) > y_of(laid, a));
}

TEST_CASE("the layout starts where the cards were") {
    Board board;
    const auto a = board.node("a", 100, 50);
    const auto b = board.node("b", 400, 300);
    board.wire(a, b);

    const auto laid = board.tidy();

    CHECK(x_of(laid, a) == Approx(100));
    CHECK(y_of(laid, a) == Approx(50));
    CHECK(x_of(laid, b) == Approx(100 + CARD_W + 80));
    CHECK(y_of(laid, b) == Approx(50));
}

TEST_CASE("a card of unknown size still gets a place") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 10, 10);
    board.sizes.erase(b);
    board.wire(a, b);

    const auto laid = board.tidy();

    REQUIRE(laid.size() == 2);
    CHECK(x_of(laid, b) == Approx(CARD_W + 80));
}

TEST_CASE("an empty graph has nothing to place") {
    Graph graph;
    CHECK(tidy_layout(graph, {}).empty());
}

TEST_CASE("a card with no wires stays last when the first column is reordered") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 0, 200);
    const auto c = board.node("c", 0, 400);
    const auto lonely = board.node("lonely", 0, 600);
    std::vector<NodeId> low;
    for (int i = 0; i < 8; ++i) low.push_back(board.node("q" + std::to_string(i), 300, 100.f * i));
    for (int i = 0; i < 6; ++i) board.wire(a, low[i]);
    board.wire(b, low[6]);
    board.wire(c, low[7]);

    const auto laid = board.tidy();

    CHECK(y_of(laid, lonely) > y_of(laid, a));
    CHECK(y_of(laid, lonely) > y_of(laid, b));
    CHECK(y_of(laid, lonely) > y_of(laid, c));
}

TEST_CASE("a right-to-left sweep moves the first column to uncross the wires") {
    Board board;
    const auto a = board.node("a", 0, 0);
    const auto b = board.node("b", 0, 200);
    const auto c = board.node("c", 0, 400);
    const auto p = board.node("p", 300, 0);
    const auto q = board.node("q", 300, 200);
    const auto r = board.node("r", 300, 400);
    const auto s = board.mix("s", 600, 0);
    const auto t = board.node("t", 600, 200);
    board.wire(c, p);
    board.wire(b, q);
    board.wire(a, r);
    board.wire_into(p, out_port, s, MIX_A);
    board.wire_into(r, out_port, s, MIX_B);
    board.wire(q, t);

    const auto laid = board.tidy();

    // c feeds p, which ends above q, the card b feeds, so the first column follows.
    CHECK(y_of(laid, a) < y_of(laid, c));
    CHECK(y_of(laid, c) < y_of(laid, b));
}
