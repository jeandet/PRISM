#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/delegates/form_delegates.hpp>

#include <cstdint>
#include <limits>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app;
}
using namespace prism;
using prism::app::widget_detail::route_event;

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
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); }
    void click(float x, float y) {   // widget-local
        Point p{rect().origin.x + DX{x}, rect().origin.y + DY{y}};
        route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    }
    void wheel(float dy) {
        Point p{rect().origin.x + DX{10}, rect().origin.y + DY{10}};
        route_event(tree, snap.get(), MouseScroll{p, DX{0}, DY{dy}});
    }
    void frame() { snap = tree.build_snapshot(300, 200, 2); }
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (auto& dl : snap->draw_lists)
            for (auto& cmd : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&cmd)) out.push_back(t->text);
        return out;
    }
};

enum class Mode { Off, Eco, Boost };
}

// ---- Radio group ----

TEST_CASE("radio group: a click selects the row under the pointer, notifying once") {
    Window<RadioGroup> w{RadioGroup{.selected = 0, .options = {"Low", "Medium", "High"}}};
    int changes = 0;
    auto c = w.model.f.on_change().connect([&](const RadioGroup&) { ++changes; });
    w.click(20, radio_detail::pad + radio_detail::row_h * 2 + 5);
    CHECK(w.value().selected == 2);
    CHECK(changes == 1);
}

TEST_CASE("radio group: arrows move the choice and stop at the ends") {
    Window<RadioGroup> w{RadioGroup{.selected = 0, .options = {"Low", "Medium", "High"}}};
    w.key(keys::down);
    CHECK(w.value().selected == 1);
    w.key(keys::right);
    w.key(keys::right);
    CHECK(w.value().selected == 2);
    w.key(keys::up);
    w.key(keys::left);
    w.key(keys::left);
    CHECK(w.value().selected == 0);
}

TEST_CASE("radio group: an enum field shown as radio buttons") {
    Window<Radio<Mode>> w{Radio<Mode>{Mode::Off}};
    w.key(keys::down);
    CHECK(w.value().value == Mode::Eco);
    w.frame();
    auto t = w.texts();
    CHECK(std::ranges::find(t, std::string{"Boost"}) != t.end());
}

TEST_CASE("radio group: describes itself as a radio group with the chosen label") {
    Field<RadioGroup> f{{.selected = 1, .options = {"Low", "Medium"}}};
    auto info = access_info(f);
    CHECK(info.role == Role::RadioGroup);
    CHECK(info.value == "Medium");
}

TEST_CASE("radio group: as wide as its longest label") {
    WidgetNode n;
    n.theme = &test_theme;
    Field<RadioGroup> narrow{{.options = {"a", "b"}}};
    Field<RadioGroup> wide{{.options = {"a", "a much longer option"}}};
    CHECK(Widget<RadioGroup>::intrinsic_width(wide, n) > Widget<RadioGroup>::intrinsic_width(narrow, n) + 50.f);
}

// ---- ProgressBar bar ----

namespace {
float fill_width(ProgressBar p) {
    WidgetNode n;
    n.theme = &test_theme;
    n.canvas_bounds = Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{30}}};
    DrawList dl;
    Field<ProgressBar> f{p};
    Widget<ProgressBar>::record(dl, f, n);
    float w = 0.f;
    for (auto& cmd : dl.commands)
        if (auto* r = std::get_if<RoundedRect>(&cmd); r && r->color == test_theme.primary) w = r->rect.extent.w.raw();
    return w;
}
}

TEST_CASE("progress: the fill follows the value and is clamped to the track") {
    float full = fill_width({.value = 1.0});
    CHECK(full > 150.f);
    CHECK(fill_width({.value = 0.0}) == doctest::Approx(0.f));
    CHECK(fill_width({.value = 0.5}) == doctest::Approx(full / 2).epsilon(0.02));
    CHECK(fill_width({.value = 7.0}) == doctest::Approx(full));
    CHECK(fill_width({.value = -3.0}) == doctest::Approx(0.f));
    CHECK(fill_width({.value = 5.0, .min = 2.0, .max = 2.0}) == doctest::Approx(0.f));   // empty range
    CHECK(fill_width({.value = 30.0, .min = 20.0, .max = 40.0}) == doctest::Approx(full / 2).epsilon(0.02));
}

TEST_CASE("progress: display only, and describes its value") {
    CHECK(Widget<ProgressBar>::focus_policy == FocusPolicy::none);
    Field<ProgressBar> f{{.value = 0.25}};
    auto info = access_info(f);
    CHECK(info.role == Role::ProgressIndicator);
    CHECK(info.numeric_value == 0.25);
    CHECK(info.max_value == 1.0);
}

// ---- Spin box ----

TEST_CASE("spin box: arrows add the step to the value, clamped to the range") {
    Window<SpinBox<int>> w{SpinBox<int>{.value = 5, .min = 0, .max = 10, .step = 2}};
    w.key(keys::up);
    CHECK(w.value().value == 7);
    w.key(keys::down);
    CHECK(w.value().value == 5);
    for (int i = 0; i < 5; ++i) w.key(keys::up);
    CHECK(w.value().value == 10);
    for (int i = 0; i < 9; ++i) w.key(keys::down);
    CHECK(w.value().value == 0);
}

TEST_CASE("spin box: typed digits commit on Enter, clamped; Escape and bad input revert") {
    Window<SpinBox<int>> w{SpinBox<int>{.value = 5, .min = 0, .max = 50, .step = 1}};
    w.type("1");
    w.type("2");
    w.frame();
    auto t = w.texts();
    CHECK(std::ranges::find(t, std::string{"12"}) != t.end());   // what is being typed is shown
    w.key(keys::enter);
    CHECK(w.value().value == 12);

    w.type("9");
    w.type("9");
    w.key(keys::enter);
    CHECK(w.value().value == 50);

    w.type("3");
    w.key(keys::escape);
    CHECK(w.value().value == 50);

    w.type("-");   // min is 0: no minus sign
    w.type("4");
    w.key(keys::backspace);
    w.type("7");
    w.key(keys::enter);
    CHECK(w.value().value == 7);
}

TEST_CASE("spin box: the wheel and the stepper buttons step") {
    Window<SpinBox<int>> w{SpinBox<int>{.value = 5, .min = 0, .max = 10, .step = 1}};
    w.wheel(1.f);
    CHECK(w.value().value == 6);
    w.wheel(-1.f);
    CHECK(w.value().value == 5);
    Rect r = w.rect();
    w.click(r.extent.w.raw() - 6, 5);    // upper half of the stepper column
    CHECK(w.value().value == 6);
    w.click(r.extent.w.raw() - 6, 25);   // lower half
    CHECK(w.value().value == 5);
}

TEST_CASE("spin box: fractional steps display rounded to two places") {
    Window<SpinBox<double>> w{SpinBox<double>{.value = 0.0, .min = 0.0, .max = 1.0, .step = 0.1}};
    w.key(keys::up);
    w.key(keys::up);
    w.key(keys::up);
    CHECK(w.value().value == doctest::Approx(0.3));
    w.frame();
    auto t = w.texts();
    CHECK(std::ranges::find(t, std::string{"0.30"}) != t.end());
    w.type("0");
    w.type(".");
    w.type("5");
    w.type(".");   // a second point is refused
    w.key(keys::enter);
    CHECK(w.value().value == doctest::Approx(0.5));
}

TEST_CASE("spin box: describes itself with its range") {
    Field<SpinBox<int>> f{{.value = 3, .min = 1, .max = 9, .step = 2}};
    auto info = access_info(f);
    CHECK(info.role == Role::SpinButton);
    CHECK(info.numeric_value == 3.0);
    CHECK(info.min_value == 1.0);
    CHECK(info.max_value == 9.0);
    CHECK(info.step == 2.0);
}

// ---- Findings from the opencode review of 3d1939e ----

TEST_CASE("spin box: a range with max below min holds the value at min") {
    Window<SpinBox<int>> w{SpinBox<int>{.value = 5, .min = 10, .max = 0, .step = 1}};
    w.key(keys::up);
    CHECK(w.value().value == 10);
}

TEST_CASE("spin box: 64-bit values step exactly at the top of their range") {
    constexpr auto top = std::numeric_limits<std::int64_t>::max();
    Window<SpinBox<std::int64_t>> w{SpinBox<std::int64_t>{.value = top - 1, .min = 0, .max = top, .step = 1}};
    w.key(keys::up);
    CHECK(w.value().value == top);
    w.key(keys::up);
    CHECK(w.value().value == top);
    w.key(keys::down);
    CHECK(w.value().value == top - 1);
}

TEST_CASE("radio group: arrows recover when the selection is past the options") {
    Window<RadioGroup> w{RadioGroup{.selected = 4, .options = {"a", "b"}}};
    w.key(keys::up);
    CHECK(w.value().selected == 0);
}

TEST_CASE("spin box: a decimal point needs a digit before it") {
    Window<SpinBox<double>> w{SpinBox<double>{.value = 0.0, .min = -5.0, .max = 5.0, .step = 0.5}};
    w.type(".");
    w.type("-");
    w.type(".");
    w.type("2");
    w.type(".");
    w.type("5");
    w.key(keys::enter);
    CHECK(w.value().value == doctest::Approx(-2.5));
}

TEST_CASE("progress: a short fill's corners are no rounder than it is wide") {
    WidgetNode n;
    n.theme = &test_theme;
    n.canvas_bounds = Rect{Point{X{0}, Y{0}}, Size{Width{200}, Height{30}}};
    DrawList dl;
    Field<ProgressBar> f{{.value = 0.01}};
    Widget<ProgressBar>::record(dl, f, n);
    for (auto& cmd : dl.commands)
        if (auto* r = std::get_if<RoundedRect>(&cmd); r && r->color == test_theme.primary)
            CHECK(r->radius <= r->rect.extent.w.raw() / 2 + 1e-4f);
}

TEST_CASE("spin box: no step is reported when the step is zero") {
    Field<SpinBox<int>> f{{.value = 1, .min = 0, .max = 9, .step = 0}};
    CHECK_FALSE(access_info(f).step.has_value());
}

// ---- busy progress ----

TEST_CASE("busy progress: the segment slides across the track and comes back") {
    using progress_detail::busy_segment;
    float w = 200.f;
    auto a = busy_segment(w, 0.0), b = busy_segment(w, 0.4), c = busy_segment(w, 0.8);
    CHECK(a.first != b.first);                       // it moves
    for (auto s : {a, b, c}) {
        CHECK(s.first >= 0.f);
        CHECK(s.first + s.second <= w + 0.001f);      // inside the track
        CHECK(s.second > 0.f);
    }
    auto period = busy_segment(w, 0.0);
    auto again = busy_segment(w, progress_detail::busy_period_s);
    CHECK(again.first == doctest::Approx(period.first));   // periodic
}

namespace {
struct Loading {   // a view(), so the bar is placed in builds without reflection too
    Field<ProgressBar> bar{{.value = 0.0, .busy = true}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(bar); }
};
struct Static {
    Field<ProgressBar> bar{{.value = 0.5}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(bar); }
};
}

TEST_CASE("busy progress asks the tree for its next frame; the tree's tick redraws it") {
    Loading m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(300, 100, 1);
    auto wake = tree.next_wake();
    REQUIRE(wake);
    CHECK(*wake - std::chrono::steady_clock::now() < std::chrono::milliseconds(100));
    tree.clear_dirty();
    tree.tick(*wake);
    CHECK(tree.any_dirty());
}

TEST_CASE("a progress bar that is not busy needs no timed frames") {
    Static m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(300, 100, 1);
    CHECK_FALSE(tree.next_wake());
}
