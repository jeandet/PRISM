#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/view_transform.hpp>

using namespace prism::core;
using namespace prism::render;

namespace {

const ViewTransform half{Point{X{100}, Y{50}}, 0.5f};

Rect rect(float x, float y, float w, float h) { return Rect{Point{X{x}, Y{y}}, Size{Width{w}, Height{h}}}; }
Point pt(float x, float y) { return Point{X{x}, Y{y}}; }

template <class C>
C scaled(C cmd) {
    DrawList dl;
    dl.commands.emplace_back(std::move(cmd));
    scale_draw_list(dl, half);
    return std::get<C>(dl.commands.front());
}

} // namespace

TEST_CASE("a transform zooms points and rects about its origin") {
    CHECK(half.apply(pt(300, 250)) == pt(200, 150));
    CHECK(half.apply(rect(300, 250, 80, 40)) == rect(200, 150, 40, 20));
}

TEST_CASE("scaling a draw list scales every command's geometry") {
    const Color c = Color::rgba(1, 2, 3);
    CHECK(scaled(FilledRect{rect(300, 250, 80, 40), c}).rect == rect(200, 150, 40, 20));

    const auto line = scaled(Line{pt(300, 250), pt(500, 450), c, 2.f});
    CHECK(line.from == pt(200, 150));
    CHECK(line.to == pt(300, 250));
    CHECK(line.thickness == 1.f);

    const auto circle = scaled(Circle{pt(300, 250), 10.f, c, 0.f});
    CHECK(circle.center == pt(200, 150));
    CHECK(circle.radius == 5.f);
    CHECK(circle.thickness == 0.f);

    const auto text = scaled(TextCmd{"x", pt(300, 250), 14.f, c});
    CHECK(text.origin == pt(200, 150));
    CHECK(text.size == 7.f);

    CHECK(scaled(ClipPush{rect(300, 250, 80, 40)}).rect == rect(200, 150, 40, 20));

    const auto poly = scaled(prism::render::Polyline{{pt(300, 250), pt(100, 50)}, c, 4.f});
    CHECK(poly.points[0] == pt(200, 150));
    CHECK(poly.points[1] == pt(100, 50));
    CHECK(poly.thickness == 2.f);

    const auto rr = scaled(RoundedRect{rect(300, 250, 80, 40), c, 6.f, 0.f});
    CHECK(rr.rect == rect(200, 150, 40, 20));
    CHECK(rr.radius == 3.f);
}

TEST_CASE("scaling leaves a clip pop alone") {
    DrawList dl;
    dl.clip_pop();
    scale_draw_list(dl, half);
    CHECK(std::holds_alternative<ClipPop>(dl.commands.front()));
}
