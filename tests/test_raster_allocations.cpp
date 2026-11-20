// Drawing must not allocate once warmed up: a canvas reused across frames (retarget()) draws
// the same content a second time with no heap allocation. Counted with tests/alloc_counter.hpp.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/raster/canvas.hpp>
#include <prism/raster/draw_snapshot.hpp>
#include <prism/raster/fonts.hpp>

#include <string>
#include <vector>

using namespace prism::core;
using prism::test::allocations;

namespace {
constexpr int W = 480, H = 272;

struct DemoModel {
    Field<std::string> title{"PRISM raster demo"};
    Field<prism::ui::Label<>> heading{{.value = "counter demo", .heading = true}};
    Field<prism::ui::Button> run{{"Run"}};
    Field<prism::ui::Slider<>> volume{{.value = 0.5}};

    void view(prism::app::ViewBuilder& vb) { vb.vstack(title, heading, run, volume); }
};

} // namespace

TEST_CASE("raster: a reused canvas draws a thick polyline without allocating")
{
    std::vector<std::uint32_t> a(std::size_t(W) * H), b(std::size_t(W) * H);
    std::vector<Point> zigzag;
    for (int i = 0; i < 100; ++i)
        zigzag.push_back(Point{X{float(4 * i)}, Y{float(i % 2 ? 40 : 120)}});
    const Rect clip{Point{X{10.f}, Y{10.f}}, Size{Width{400.f}, Height{200.f}}};

    prism::raster::Canvas canvas{a, W, H, W};
    canvas.push_clip(clip);
    canvas.polyline(zigzag, Color::rgba(0, 140, 200), 2.f); // warm-up frame
    canvas.pop_clip();

    canvas.retarget(b);
    const auto before = allocations();
    canvas.push_clip(clip);
    canvas.polyline(zigzag, Color::rgba(0, 140, 200), 2.f);
    canvas.pop_clip();
    CHECK(allocations() - before == 0);
    CHECK(a == b); // the same pixels, in the new target
}

TEST_CASE("raster: the second frame of a laid-out snapshot draws without allocating")
{
    DemoModel model;
    prism::app::WidgetTree tree(model);
    prism::raster::Fonts fonts;
    tree.set_text_measurer(&fonts);
    auto snap = tree.build_snapshot(W, H, 1);
    REQUIRE(snap != nullptr);
    std::vector<std::uint32_t> a(std::size_t(W) * H), b(std::size_t(W) * H);

    prism::raster::Canvas canvas{a, W, H, W};
    prism::raster::draw(canvas, *snap, fonts); // warm-up: glyph cache, scratch buffers

    canvas.retarget(b);
    const auto before = allocations();
    prism::raster::draw(canvas, *snap, fonts);
    CHECK(allocations() - before == 0);
}
