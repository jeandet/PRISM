// Steady-state frames must not allocate (doc/design/allocation-free-frames.md): each test warms
// a path up once, then counts the heap allocations of doing it again.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "alloc_counter.hpp"

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/widgets/plot.hpp>

using namespace prism;
using namespace prism::plot;
using prism::test::allocations;

namespace {

struct Controls {
    Field<ui::Label<>> title{{.value = "controls", .heading = true}};
    Field<ui::Checkbox> enabled{{.checked = true, .label = "enabled"}};
    Field<ui::Slider<>> volume{{.value = 0.5, .min = 0.0, .max = 1.0, .label = "volume"}};
    void view(app::ViewBuilder& vb) { vb.vstack(title, enabled, volume); }
};

struct ManyLabels {
    Field<ui::Label<>> a{{.value = "one"}}, b{{.value = "two"}}, c{{.value = "three"}},
                       d{{.value = "four"}}, e{{.value = "five"}};
    void view(app::ViewBuilder& vb) { vb.vstack(a, b, c, d, e); }
};

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

TEST_CASE("a snapshot rebuilt into the same storage allocates nothing when nothing changed")
{
    Controls model;
    app::WidgetTree tree(model);
    render::SceneSnapshot snap;
    tree.build_snapshot(snap, 480.f, 272.f, 1); // warm-up: layout, per-widget caches
    tree.clear_dirty();
    tree.build_snapshot(snap, 480.f, 272.f, 2); // warm-up: the snapshot's capacities
    tree.clear_dirty();
    const auto before = allocations();
    tree.build_snapshot(snap, 480.f, 272.f, 3);
    CHECK(allocations() - before == 0);
    CHECK(snap.version == 3);
}

TEST_CASE("a rebuilt snapshot keeps nothing from the previous scene")
{
    ManyLabels big;
    app::WidgetTree big_tree(big);
    Controls small;
    app::WidgetTree small_tree(small);

    render::SceneSnapshot reused;
    big_tree.build_snapshot(reused, 480.f, 272.f, 1);
    small_tree.build_snapshot(reused, 480.f, 272.f, 2);
    const auto fresh = small_tree.build_snapshot(480.f, 272.f, 3);

    CHECK(reused.geometry == fresh->geometry);
    CHECK(reused.z_order == fresh->z_order);
    CHECK(reused.draw_lists.size() == fresh->draw_lists.size());
    CHECK(reused.visible.size() == fresh->visible.size());
    CHECK(reused.scale.size() == fresh->scale.size());
    CHECK(reused.overlay.size() == fresh->overlay.size());
    CHECK(reused.overlay_geometry == fresh->overlay_geometry);
}

TEST_CASE("mutate writes a label's text into its existing capacity")
{
    Field<ui::Label<>> stats{{.value = "measuring, please wait for the first line"}};
    const auto before = allocations();
    stats.mutate([](ui::Label<>& l) { l.value.assign("12 fps \u00b7 0 allocs/frame"); });
    CHECK(allocations() - before == 0);
    CHECK(stats.get().value == "12 fps \u00b7 0 allocs/frame");
}
