#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/draw_list.hpp>
#include <prism/render/svg_export.hpp>

#include <vector>

using namespace prism::render;
using namespace prism::core;

namespace {
Point P(float x, float y) { return {X{x}, Y{y}}; }
Rect R(float x, float y, float w, float h) { return {P(x, y), Size{Width{w}, Height{h}}}; }
}

TEST_CASE("an image holds RGBA pixels and rejects a size mismatch") {
    auto ok = Image::from_rgba(2, 1, std::vector<std::uint8_t>(8, 255));
    REQUIRE(ok);
    CHECK((*ok)->width() == 2);
    CHECK((*ok)->height() == 1);
    CHECK_FALSE(Image::from_rgba(2, 2, std::vector<std::uint8_t>(8, 255)));   // needs 16 bytes
    CHECK_FALSE(Image::from_rgba(0, 1, {}));
}

TEST_CASE("every image has its own id, so backends can cache uploads by it") {
    auto a = *Image::from_rgba(1, 1, {0, 0, 0, 255});
    auto b = *Image::from_rgba(1, 1, {0, 0, 0, 255});
    CHECK(a->id() != b->id());
}

TEST_CASE("an image command is translated like any other and counts in the bounding box") {
    auto img = *Image::from_rgba(1, 1, {255, 0, 0, 255});
    DrawList dl;
    dl.clip_push(P(10, 20), Size{Width{100}, Height{100}});
    dl.image(img, R(5, 5, 16, 16));
    dl.clip_pop();
    auto* cmd = std::get_if<ImageCmd>(&dl.commands[1]);
    REQUIRE(cmd);
    CHECK(cmd->rect.origin.x.raw() == doctest::Approx(15.f));
    CHECK(cmd->rect.origin.y.raw() == doctest::Approx(25.f));
    CHECK(cmd->image == img);
}

TEST_CASE("SVG export draws an image as a placeholder of the right size") {
    auto img = *Image::from_rgba(1, 1, {255, 0, 0, 255});
    DrawList dl;
    dl.image(img, R(5, 6, 16, 17));
    auto svg = prism::render::to_svg(dl);
    CHECK(svg.find("x=\"5\" y=\"6\" width=\"16\" height=\"17\"") != std::string::npos);
}
