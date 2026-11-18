#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/nodes/board_view.hpp>

#include <vector>

using namespace prism::core;
using namespace prism::nodes;
using doctest::Approx;

namespace {

Rect rect(float x, float y, float w, float h) { return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
Point pt(float x, float y) { return Point{X{x}, Y{y}}; }

Point board_point_under(BoardView v, Offset pointer) {
    return pt(pointer.dx.raw() / v.zoom - v.pan.x.raw(), pointer.dy.raw() / v.zoom - v.pan.y.raw());
}

Point on_board(BoardView v, Point p) {
    return pt(v.zoom * (p.x.raw() + v.pan.x.raw()), v.zoom * (p.y.raw() + v.pan.y.raw()));
}

} // namespace

TEST_CASE("zoom is clamped to its range") {
    CHECK(clamp_zoom(0.1f) == 0.25f);
    CHECK(clamp_zoom(5.f) == 2.f);
    CHECK(clamp_zoom(1.f) == 1.f);
}

TEST_CASE("a wheel notch multiplies the zoom by 1.1, within the range") {
    CHECK(wheel_zoom(1.f, 1.f) == Approx(1.1f));
    CHECK(wheel_zoom(1.9f, 3.f) == 2.f);
    CHECK(wheel_zoom(0.3f, -5.f) == 0.25f);
}

TEST_CASE("zooming about the pointer keeps the board point under it") {
    const BoardView v{pt(10, 0), 1.f};
    const Offset pointer{DX{110}, DY{40}};
    CHECK(board_point_under(v, pointer) == pt(100, 40));

    const auto z = zoomed_about(v, pointer, 2.f);
    CHECK(z.zoom == 2.f);
    CHECK(board_point_under(z, pointer).x.raw() == Approx(100.f));
    CHECK(board_point_under(z, pointer).y.raw() == Approx(40.f));

    const auto clamped = zoomed_about(v, pointer, 9.f);
    CHECK(clamped.zoom == 2.f);
    CHECK(board_point_under(clamped, pointer).x.raw() == Approx(100.f));
}

TEST_CASE("graph_point is where a board offset lies in the graph: the inverse of a node's place") {
    const BoardView v{pt(10, -20), 0.5f};
    const Point node = pt(100, 40);
    const Point placed = on_board(v, node);
    const Point back = graph_point(v, placed - Point{});
    CHECK(back.x.raw() == Approx(node.x.raw()));
    CHECK(back.y.raw() == Approx(node.y.raw()));
    CHECK(graph_point(BoardView{}, Offset{DX{7}, DY{9}}) == pt(7, 9));
}

TEST_CASE("board_point is where a graph point lies on the board: zoom * (position + pan), graph_point's inverse") {
    const BoardView v{pt(10, -20), 0.5f};
    CHECK(board_point(v, pt(100, 40)) == pt(55, 10));
    const Point back = graph_point(v, board_point(v, pt(100, 40)) - Point{});
    CHECK(back.x.raw() == Approx(100.f));
    CHECK(back.y.raw() == Approx(40.f));
    CHECK(board_point(BoardView{}, pt(7, 9)) == pt(7, 9));
}

TEST_CASE("fit_all shows every card with the margin clear, at the largest such zoom") {
    const std::vector<Rect> cards{rect(0, 0, 200, 100), rect(600, 300, 200, 100)};
    const auto v = fit_all(cards, Size{Width{500}, Height{300}});
    CHECK(v.zoom == Approx(std::min(452.f / 800.f, 252.f / 400.f)));
    for (const auto& c : cards) {
        const auto tl = on_board(v, c.origin);
        const auto br = on_board(v, pt(c.origin.x.raw() + c.extent.w.raw(), c.origin.y.raw() + c.extent.h.raw()));
        CHECK(tl.x.raw() >= Approx(24.f));
        CHECK(tl.y.raw() >= Approx(24.f));
        CHECK(br.x.raw() <= Approx(476.f));
        CHECK(br.y.raw() <= Approx(276.f));
    }
}

TEST_CASE("fit_all keeps a small card at natural size, centred") {
    const std::vector<Rect> cards{rect(10, 20, 100, 50)};
    const auto v = fit_all(cards, Size{Width{500}, Height{300}});
    CHECK(v.zoom == 1.f);
    const auto centre = on_board(v, pt(60, 45));
    CHECK(centre.x.raw() == Approx(250.f));
    CHECK(centre.y.raw() == Approx(150.f));
}

TEST_CASE("fit_all of nothing is the default view") {
    CHECK(fit_all({}, Size{Width{500}, Height{300}}) == BoardView{});
}
