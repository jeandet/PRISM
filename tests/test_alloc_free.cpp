// Steady-state frames must not allocate (doc/design/allocation-free-frames.md): each test warms
// a path up once, then counts the heap allocations of doing it again.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"

#include <prism/widgets/plot.hpp>

using namespace prism;
using namespace prism::plot;
using prism::test::allocations;

namespace {

Rect plot_bounds() { return Rect{Point{X{0}, Y{0}}, Size{Width{400}, Height{300}}}; }

} // namespace

TEST_CASE("an empty plot re-records without allocating")
{
    Theme t = default_theme();
    WidgetNode node;
    node.theme = &t;
    node.canvas_bounds = plot_bounds();
    PlotModel plot;
    plot.x_range.set(AxisRange{0.0, 10.0, false});
    plot.y_range.set(AxisRange{-1.0, 1.0, false});
    plot.x_label.set("x");
    plot.y_label.set("y");

    DrawList dl;
    plot.canvas(dl, plot_bounds(), node); // warm-up: the list's capacity
    dl.clear();
    const auto before = allocations();
    plot.canvas(dl, plot_bounds(), node);
    CHECK(allocations() - before == 0);
}

TEST_CASE("nice_ticks never exceeds its capacity")
{
    CHECK(nice_ticks(0.0, 1.0, 1000).size() <= TickList::capacity);
}
