#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include "../examples/widget_gallery/knob.hpp"

#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app;
}
using namespace prism;
using prism::app::widget_detail::route_event;
using gallery::Knob;
namespace kd = gallery::knob_detail;

namespace {
Theme test_theme;

// A one-widget window: routes real events, so focus, capture and localization are exercised.
template <typename T>
struct One {
    Field<T> f;
    explicit One(T v) : f(std::move(v)) {}
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(f); }
};

template <typename T>
struct Window {
    One<T> model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap;
    explicit Window(T v) : model(std::move(v)) {
        snap = tree.build_snapshot(300, 200, 1);
        tree.set_focused(tree.leaf_ids()[0]);
    }
    const T& value() const { return model.f.get(); }
    Rect rect() const { return *find_widget_rect(*snap, tree.leaf_ids()[0]); }
    Point at(float x, float y) const {   // widget-local
        return Point{rect().origin.x + DX{x}, rect().origin.y + DY{y}};
    }
    void key(std::int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); }
    void press(float x, float y) { route_event(tree, snap.get(), MouseButton{at(x, y), buttons::left, true}); }
    void move_to(float x, float y) {
        route_event(tree, snap.get(), MouseMove{at(x, y), buttons::left});
    }
    void release(float x, float y) {
        route_event(tree, snap.get(), MouseButton{at(x, y), buttons::left, false});
    }
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (auto& dl : snap->draw_lists)
            for (auto& cmd : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&cmd)) out.push_back(t->text);
        return out;
    }
};

const Knob tenths{.value = 5.0, .min = 0.0, .max = 10.0, .step = 0.5};   // 0..10 in halves
}

// ---- Geometry: no widget, no draw list, no theme ----

TEST_CASE("knob geometry: a value's place on the arc") {
    CHECK(kd::fraction(tenths) == doctest::Approx(0.5));
    CHECK(kd::fraction(Knob{.value = 20.0, .max = 10.0}) == 1.0);   // clamped above
    CHECK(kd::fraction(Knob{.value = -3.0, .max = 10.0}) == 0.0);   // and below
    CHECK(kd::fraction(Knob{.value = 4.0, .min = 4.0, .max = 4.0}) == 0.0);   // no range
}

TEST_CASE("knob geometry: snapping follows the step, and a step of 0 means a hundredth") {
    CHECK(kd::snapped(tenths, 5.24) == 5.0);
    CHECK(kd::snapped(tenths, 5.26) == 5.5);
    CHECK(kd::snapped(tenths, 99.0) == 10.0);
    CHECK(kd::snapped(tenths, -99.0) == 0.0);
    const Knob continuous{.min = 0.0, .max = 1.0};
    CHECK(kd::snapped(continuous, 0.1234) == doctest::Approx(0.12));
}

TEST_CASE("knob geometry: the hand points at the pointer") {
    const Knob k{.value = 0.0, .min = 0.0, .max = 12.0, .step = 0.5};
    CHECK(kd::turned(k, DX{0.f}, DY{-30.f}) == 6.0);    // straight up: half way
    CHECK(kd::turned(k, DX{-30.f}, DY{0.f}) == 2.0);    // left: 45 of 270 degrees
    CHECK(kd::turned(k, DX{30.f}, DY{0.f}) == 10.0);    // right: 225 of 270 degrees
    CHECK(kd::turned(k, DX{-20.f}, DY{20.f}) == 0.0);   // the lower left is the start
}

TEST_CASE("knob geometry: the gap at the bottom holds the nearer end, and the centre holds still") {
    const Knob high{.value = 9.0, .min = 0.0, .max = 12.0};
    const Knob low{.value = 3.0, .min = 0.0, .max = 12.0};
    CHECK(kd::turned(high, DX{-5.f}, DY{30.f}) == 12.0);   // past the bottom from the right: max
    CHECK(kd::turned(low, DX{5.f}, DY{30.f}) == 0.0);      // ... and from the left: min
    CHECK(kd::turned(low, DX{1.f}, DY{-1.f}) == 3.0);      // too near the centre to have an angle
}

TEST_CASE("knob geometry: an arrow is one step, and the ends hold") {
    CHECK(kd::nudged(tenths, 1) == 5.5);
    CHECK(kd::nudged(tenths, -1) == 4.5);
    CHECK(kd::nudged(Knob{.value = 10.0, .min = 0.0, .max = 10.0, .step = 0.5}, 1) == 10.0);
    CHECK(kd::nudged(Knob{.value = 0.0, .min = 0.0, .max = 10.0, .step = 0.5}, -1) == 0.0);
}

TEST_CASE("knob geometry: 270 degrees of travel, from the lower left to the lower right") {
    constexpr float deg = std::numbers::pi_v<float> / 180.f;
    CHECK(kd::angle_at(0.0) == doctest::Approx(135.f * deg));   // min: 8 o'clock
    CHECK(kd::angle_at(0.5) == doctest::Approx(270.f * deg));   // half: straight up
    CHECK(kd::angle_at(1.0) == doctest::Approx(405.f * deg));   // max: 4 o'clock
}

// ---- record() ----

TEST_CASE("knob record: a body, a track, the filled part, one pointer, and both captions") {
    WidgetNode node;
    node.theme = &test_theme;
    node.canvas_bounds = Rect{Point{X{0}, Y{0}}, Size{Width{72}, Height{76}}};
    Field<Knob> f{Knob{.value = 2400.0, .min = 100.0, .max = 12000.0, .step = 10.0,
                       .label = "Cutoff", .unit = "Hz"}};
    DrawList dl;
    Widget<Knob>::record(dl, f, node);

    int arcs = 0, pointers = 0, discs = 0;
    std::vector<std::string> texts;
    for (auto& cmd : dl.commands) {
        if (auto* t = std::get_if<TextCmd>(&cmd)) texts.push_back(t->text);
        else if (std::holds_alternative<Circle>(cmd)) ++discs;
        else if (std::holds_alternative<Polyline>(cmd)) ++arcs;
        else if (std::holds_alternative<Line>(cmd)) ++pointers;
    }
    CHECK(discs == 2);       // the dial's body, then its edge
    CHECK(arcs == 2);        // the whole track, then the part up to the value
    CHECK(pointers == 1);
    REQUIRE(texts.size() == 2);
    CHECK(texts[0] == "Cutoff");
    CHECK(texts[1] == "2400 Hz");   // the step's precision, then the unit
    CHECK(dl.bounding_box().extent.h.raw() == doctest::Approx(76));
}

TEST_CASE("knob record: the value text follows the value, not the theme") {
    WidgetNode node;
    node.theme = &test_theme;
    node.canvas_bounds = Rect{Point{X{0}, Y{0}}, Size{Width{72}, Height{76}}};
    Field<Knob> f{Knob{.value = -0.25, .min = -1.0, .max = 1.0, .step = 0.05, .label = "Pan"}};
    DrawList dl;
    Widget<Knob>::record(dl, f, node);
    const auto* last = std::get_if<TextCmd>(&dl.commands.back());
    REQUIRE(last != nullptr);
    CHECK(last->text == "-0.25");
}

// ---- Through the router: focus, keys, capture ----

TEST_CASE("knob input: an arrow key is one step, and other keys are not its business") {
    Window<Knob> w{tenths};
    w.key(keys::up);
    CHECK(w.value().value == 5.5);
    w.key(keys::right);
    CHECK(w.value().value == 6.0);
    w.key(keys::down);
    CHECK(w.value().value == 5.5);
    w.key(keys::left);
    CHECK(w.value().value == 5.0);
    w.key(keys::home);
    CHECK(w.value().value == 5.0);
}

TEST_CASE("knob input: dragging turns the hand towards the pointer, and the release ends it") {
    Window<Knob> w{Knob{.value = 0.0, .min = 0.0, .max = 12.0, .step = 0.5}};
    const float cx = w.rect().extent.w.raw() / 2.f, cy = 26.f;   // the dial's centre, in the knob
    w.press(cx, cy + 10.f);
    w.move_to(cx, cy - 30.f);   // above the centre
    CHECK(w.value().value == 6.0);
    w.move_to(cx + 30.f, cy);   // to its right: the hand goes on clockwise
    CHECK(w.value().value == 10.0);
    w.move_to(cx + 30.f, cy - 30.f);   // up and right: 180 of 270 degrees, back up with the pointer
    CHECK(w.value().value == 8.0);
    w.release(cx + 30.f, cy - 30.f);
    w.move_to(cx - 30.f, cy);
    CHECK(w.value().value == 8.0);
}

TEST_CASE("knob input: a click without motion leaves the value alone") {
    Window<Knob> w{tenths};
    w.press(36, 40);
    w.release(36, 40);
    CHECK(w.value().value == 5.0);
}

// ---- Accessibility ----

TEST_CASE("knob: it reports itself as a slider with its numeric range") {
    Field<Knob> f{Knob{.value = 3.0, .min = -1.0, .max = 7.0, .step = 0.5, .label = "Pan", .unit = "x"}};
    const auto info = access_info(f);
    CHECK(info.role == Role::Slider);
    CHECK(info.name == "Pan");
    CHECK(info.value == "3.0 x");
    CHECK(info.numeric_value == doctest::Approx(3.0));
    CHECK(info.min_value == doctest::Approx(-1.0));
    CHECK(info.max_value == doctest::Approx(7.0));
    CHECK(info.step == doctest::Approx(0.5));
}
