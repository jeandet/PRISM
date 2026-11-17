// A second mouse button during a drag (a right click while the left button holds a widget): the
// drag goes on and nothing stays stuck; a lost release of a right press freezes nothing.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {

Point P(float x, float y) { return {X{x}, Y{y}}; }
Point below(Point p, float dy) { return {p.x, Y{p.y.raw() + dy}}; }
Point right_of(Point p, float dx) { return {X{p.x.raw() + dx}, p.y}; }

template <typename Model>
struct Fixture {
    Model m;
    WidgetTree tree{m};
    float w, h;
    std::unique_ptr<SceneSnapshot> snap;
    uint64_t v = 1;
    Fixture(float w_ = 400, float h_ = 100) : w(w_), h(h_), snap(tree.build_snapshot(w, h, 1)) {}
    void frame() { snap = tree.build_snapshot(w, h, ++v); }
    void button(Point at, uint8_t b, bool pressed) { route_event(tree, snap.get(), MouseButton{at, b, pressed}); frame(); }
    void move_to(Point at) { route_event(tree, snap.get(), MouseMove{at, 1}); frame(); }
    void right_click(Point at) { button(at, buttons::right, true); button(at, buttons::right, false); }
    Rect rect_of(WidgetId id) const {
        for (auto& [gid, r] : snap->geometry) if (gid == id) return r;
        FAIL("no geometry");
        return {};
    }
};

struct Scrolling {
    Field<int> a{0}, b{0}, c{0}, d{0}, e{0}, f{0}, g{0}, h{0};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] { for (auto* x : {&a, &b, &c, &d, &e, &f, &g, &h}) vb.widget(*x); });
    }
};

Point thumb_center(const SceneSnapshot& s) {   // the thumb comes first, then its track
    REQUIRE(!s.overlay_geometry.empty());
    return s.overlay_geometry.front().second.center();
}

struct OneSlider {
    Field<Slider<>> s{{.value = 0.0}};
    WidgetId id = 0;
    void view(WidgetTree::ViewBuilder& vb) { id = vb.widget(s).node_ref.id; }
};

struct TwoPanes {
    Field<int> a{0}, b{0};
    WidgetId left = 0, handle = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            left = vb.widget(a).node_ref.id;
            vb.handle();
            handle = left + 1;
            vb.widget(b);
        });
    }
};

struct TwoButtons {
    Field<Button> a{{.text = "A"}}, b{{.text = "B"}};
    WidgetId a_id = 0, b_id = 0;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            a_id = vb.widget(a).node_ref.id;
            b_id = vb.widget(b).node_ref.id;
        });
    }
};

} // namespace

TEST_CASE("a right click during a scrollbar drag neither ends nor sticks it") {
    Fixture<Scrolling> f;
    const Point grab = thumb_center(*f.snap);
    f.button(grab, buttons::left, true);
    REQUIRE(f.tree.in_scrollbar_drag());
    f.right_click(grab);
    CHECK(f.tree.in_scrollbar_drag());
    const float y0 = thumb_center(*f.snap).y.raw();
    f.move_to(below(grab, 20));
    CHECK(thumb_center(*f.snap).y.raw() > y0);
    f.button(below(grab, 20), buttons::left, false);
    CHECK_FALSE(f.tree.in_scrollbar_drag());
    CHECK(f.tree.captured_id() == 0);
}

TEST_CASE("a right click on a scrollbar starts no drag") {
    Fixture<Scrolling> f;
    const Point at = thumb_center(*f.snap);
    f.right_click(at);
    CHECK_FALSE(f.tree.in_scrollbar_drag());
    CHECK(f.tree.captured_id() == 0);
}

TEST_CASE("a right click during a slider drag neither ends nor sticks it") {
    Fixture<OneSlider> f;
    const Rect r = f.rect_of(f.m.id);
    const Point quarter = P(r.origin.x.raw() + r.extent.w.raw() * 0.25f, r.center().y.raw());
    f.button(quarter, buttons::left, true);
    const double at_quarter = f.m.s.get().value;
    f.right_click(quarter);
    f.move_to(right_of(quarter, r.extent.w.raw() * 0.5f));
    CHECK(f.m.s.get().value > at_quarter);
    f.button(right_of(quarter, r.extent.w.raw() * 0.5f), buttons::left, false);
    CHECK(f.tree.captured_id() == 0);
}

TEST_CASE("a right click during a split-handle drag neither ends nor sticks it") {
    Fixture<TwoPanes> f(406, 100);
    const float w0 = f.rect_of(f.m.left).extent.w.raw();
    const Point grab = f.rect_of(f.m.handle).center();
    f.button(grab, buttons::left, true);
    REQUIRE(f.tree.in_split_drag());
    f.move_to(right_of(grab, 10));
    f.right_click(right_of(grab, 10));
    CHECK(f.tree.in_split_drag());
    f.move_to(right_of(grab, 20));
    CHECK(f.rect_of(f.m.left).extent.w.raw() == doctest::Approx(w0 + 20));
    f.button(right_of(grab, 20), buttons::left, false);
    CHECK_FALSE(f.tree.in_split_drag());
    CHECK(f.tree.captured_id() == 0);
}

TEST_CASE("a right press whose release is lost leaves left clicks working") {
    Fixture<TwoButtons> f;
    f.button(f.rect_of(f.m.a_id).center(), buttons::right, true);   // the release never comes
    const Point on_b = f.rect_of(f.m.b_id).center();
    f.button(on_b, buttons::left, true);
    f.button(on_b, buttons::left, false);
    CHECK(f.m.b.get().click_count == 1);
}
