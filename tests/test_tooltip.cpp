#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
#include <prism/ui/tooltip.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::ui;
using namespace std::chrono_literals;

namespace {
const TooltipState::clock::time_point t0{};
const Point at{X{50}, Y{40}};
}

TEST_CASE("tooltip: hovering a widget with a tooltip arms it, without showing it yet") {
    auto s = tooltip_hovered({}, 7, true, at, t0);
    CHECK(s.target == 7);
    CHECK_FALSE(s.shown);
    REQUIRE(tooltip_wake(s));
    CHECK(*tooltip_wake(s) == t0 + tooltip_delay);
}

TEST_CASE("tooltip: a widget without one arms nothing") {
    auto s = tooltip_hovered({}, 7, false, at, t0);
    CHECK(s.target == 0);
    CHECK_FALSE(tooltip_wake(s));
}

TEST_CASE("tooltip: shows once the delay has passed, not before") {
    auto s = tooltip_hovered({}, 7, true, at, t0);
    CHECK_FALSE(tooltip_ticked(s, t0 + tooltip_delay - 1ms).shown);
    auto shown = tooltip_ticked(s, t0 + tooltip_delay);
    CHECK(shown.shown);
    CHECK_FALSE(tooltip_wake(shown));   // nothing left to wait for
}

TEST_CASE("tooltip: moving within the same widget keeps the timer; the box follows the pointer until shown") {
    auto s = tooltip_hovered({}, 7, true, at, t0);
    Point later{X{60}, Y{45}};
    auto moved = tooltip_hovered(s, 7, true, later, t0 + 300ms);
    CHECK(*tooltip_wake(moved) == t0 + tooltip_delay);
    CHECK(moved.pointer == later);
    auto shown = tooltip_ticked(moved, t0 + tooltip_delay);
    auto after = tooltip_hovered(shown, 7, true, at, t0 + 1s);
    CHECK(after.shown);
    CHECK(after.pointer == later);   // a shown tooltip stays put
}

TEST_CASE("tooltip: leaving the widget hides it; another widget starts a new wait") {
    auto shown = tooltip_ticked(tooltip_hovered({}, 7, true, at, t0), t0 + tooltip_delay);
    auto other = tooltip_hovered(shown, 8, true, at, t0 + 2s);
    CHECK(other.target == 8);
    CHECK_FALSE(other.shown);
    CHECK(*tooltip_wake(other) == t0 + 2s + tooltip_delay);
    CHECK(tooltip_hovered(shown, 0, false, at, t0 + 2s).target == 0);
}

TEST_CASE("tooltip: dismissed (click, key, scroll) stays hidden until the pointer leaves the widget") {
    auto shown = tooltip_ticked(tooltip_hovered({}, 7, true, at, t0), t0 + tooltip_delay);
    auto d = tooltip_dismissed(shown);
    CHECK_FALSE(d.shown);
    CHECK_FALSE(tooltip_wake(d));
    CHECK_FALSE(tooltip_ticked(d, t0 + 10s).shown);
    CHECK_FALSE(tooltip_wake(tooltip_hovered(d, 7, true, at, t0 + 10s)));   // same widget: still quiet
    CHECK(tooltip_wake(tooltip_hovered(d, 8, true, at, t0 + 10s)));          // another one: armed
    auto pending = tooltip_dismissed(tooltip_hovered({}, 7, true, at, t0));   // dismissed before showing
    CHECK_FALSE(tooltip_ticked(pending, t0 + tooltip_delay).shown);
}

TEST_CASE("tooltip: if the hovered widget loses its tooltip, the state lets go of it") {
    auto shown = tooltip_ticked(tooltip_hovered({}, 7, true, at, t0), t0 + tooltip_delay);
    auto s = tooltip_hovered(shown, 7, false, at, t0 + 1s);
    CHECK(s.target == 0);
    CHECK_FALSE(s.shown);
}

TEST_CASE("tooltip: the box is as tall as the measurer's line, plus padding") {
    struct TallLines final : TextMeasurer {
        float width(std::string_view t, float, FontRole) const override { return 7.f * static_cast<float>(t.size()); }
        float line_height(float, FontRole) const override { return 30.f; }
    } tall;
    Theme theme;
    theme.measurer = &tall;
    render::DrawList dl;
    draw_tooltip(dl, "hi", at, Size{Width{400}, Height{300}}, theme);
    auto* box = std::get_if<render::FilledRect>(&dl.commands.front());
    REQUIRE(box);
    CHECK(box->rect.extent.h.raw() == doctest::Approx(30.f + 2 * tooltip_metrics::pad_y));
}

TEST_CASE("tooltip: placed below the pointer, inside the window") {
    Size text{Width{100}, Height{18}};
    Size window{Width{400}, Height{300}};
    Rect r = tooltip_rect(at, text, window);
    CHECK(r.origin.y.raw() > at.y.raw());                        // under the cursor, not on it
    CHECK(r.extent.w.raw() > text.w.raw());                      // padded
    CHECK(r.origin.x.raw() == doctest::Approx(at.x.raw()));
}

TEST_CASE("tooltip: near the bottom edge it flips above the pointer; near the right edge it slides left") {
    Size text{Width{100}, Height{18}};
    Size window{Width{400}, Height{300}};
    Point corner{X{390}, Y{295}};
    Rect r = tooltip_rect(corner, text, window);
    CHECK(r.origin.y.raw() + r.extent.h.raw() < corner.y.raw());   // above, not covering the pointer
    CHECK(r.origin.x.raw() + r.extent.w.raw() <= 400.f);
    CHECK(r.origin.x.raw() >= 0.f);
    CHECK(r.origin.y.raw() >= 0.f);
}

// --- in a widget tree -------------------------------------------------------

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;
using namespace prism::input;

namespace {
struct Toolbar {
    Field<Button> save{{"Save"}};
    Field<Button> plain{{"Plain"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(save).tooltip("Write the file to disk");
            vb.widget(plain);
        });
    }
};

bool overlay_has(const SceneSnapshot& snap, std::string_view text) {
    for (auto& cmd : snap.overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&cmd); t && t->text == text) return true;
    return false;
}

template <typename Model>
struct TreeFixture {
    Model model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 300, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 300, ++v); }
    Point centre_of_widget_with(std::string_view tip) const {
        for (auto& [id, r] : snap->geometry)
            if (tree.tooltip_of(id) == tip)
                return Point{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
        FAIL("no widget with that tooltip");
        return {};
    }
    Point centre_of_last_widget() const {
        auto& [id, r] = snap->geometry.back();
        return Point{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
    }
    void move(Point p) { route_event(tree, snap.get(), MouseMove{p}); }
    // Runs the tree's pending timed wake, as the app's timer would, and draws a frame.
    void wait_for_tooltip() {
        auto due = tree.tooltip_wake();
        REQUIRE(due);
        tree.tick_tooltip(*due);
        frame();
    }
};
}

TEST_CASE("tooltip in a tree: hovering shows it after the delay, in the overlay") {
    TreeFixture<Toolbar> f;
    f.move(f.centre_of_widget_with("Write the file to disk"));
    f.frame();
    CHECK_FALSE(overlay_has(*f.snap, "Write the file to disk"));   // not yet
    f.wait_for_tooltip();
    CHECK(overlay_has(*f.snap, "Write the file to disk"));
}

TEST_CASE("tooltip in a tree: showing it marks the tree dirty, so the app publishes a frame") {
    TreeFixture<Toolbar> f;
    f.move(f.centre_of_widget_with("Write the file to disk"));
    f.frame();
    f.tree.clear_dirty();
    f.tree.tick_tooltip(*f.tree.tooltip_wake());
    CHECK(f.tree.any_dirty());
}

TEST_CASE("tooltip in a tree: a click hides it, and it stays hidden while the pointer stays") {
    TreeFixture<Toolbar> f;
    auto p = f.centre_of_widget_with("Write the file to disk");
    f.move(p);
    f.wait_for_tooltip();
    route_event(f.tree, f.snap.get(), MouseButton{p, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseButton{p, buttons::left, false});
    f.move(p);
    f.frame();
    CHECK_FALSE(overlay_has(*f.snap, "Write the file to disk"));
    CHECK_FALSE(f.tree.tooltip_wake());
}

TEST_CASE("tooltip in a tree: moving onto a widget without one hides it") {
    TreeFixture<Toolbar> f;
    f.move(f.centre_of_widget_with("Write the file to disk"));
    f.wait_for_tooltip();
    f.move(f.centre_of_last_widget());
    f.frame();
    CHECK_FALSE(overlay_has(*f.snap, "Write the file to disk"));
    CHECK_FALSE(f.tree.tooltip_wake());
}

TEST_CASE("tooltip in a tree: no tooltip while dragging (a widget holds the mouse)") {
    TreeFixture<Toolbar> f;
    auto other = f.centre_of_last_widget();
    route_event(f.tree, f.snap.get(), MouseButton{other, buttons::left, true});   // capture starts
    f.move(f.centre_of_widget_with("Write the file to disk"));
    CHECK_FALSE(f.tree.tooltip_wake());
}

TEST_CASE("tooltip in a tree: text input, paste and composition hide it too") {
    for (InputEvent ev : {InputEvent{TextInput{"a"}}, InputEvent{Paste{"b"}}, InputEvent{Composition{}}}) {
        TreeFixture<Toolbar> f;
        f.move(f.centre_of_widget_with("Write the file to disk"));
        f.wait_for_tooltip();
        route_event(f.tree, f.snap.get(), ev);
        f.frame();
        CHECK_FALSE(overlay_has(*f.snap, "Write the file to disk"));
    }
}

namespace {
struct WithDialog {
    Field<Button> save{{"Save"}};
    Field<Dialog> ask{{.title = "Unsaved changes", .message = "Discard?", .buttons = {"Discard", "Cancel"}}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(save).tooltip("Write the file to disk");
            vb.dialog(ask);
        });
    }
};
}

TEST_CASE("tooltip in a tree: a popup opened by the app hides a shown tooltip") {
    TreeFixture<WithDialog> f;
    f.move(f.centre_of_widget_with("Write the file to disk"));
    f.wait_for_tooltip();
    REQUIRE(overlay_has(*f.snap, "Write the file to disk"));
    auto d = f.model.ask.get();
    d.open = true;
    f.model.ask.set(d);   // no pointer event
    f.frame();
    CHECK_FALSE(overlay_has(*f.snap, "Write the file to disk"));
}

// The handle points into the builder's node list: it may only be used in the statement that
// placed the widget, so a stored handle must not compile.
template <typename H>
concept tooltip_on_lvalue = requires(H& h) { h.tooltip("x"); };
static_assert(!tooltip_on_lvalue<WidgetTree::ViewBuilder::LeafHandle>);

#if __cpp_impl_reflection
namespace {
struct Readouts {
    Field<int> raw{3};
    [[=prism::core::tooltip<"Twice the raw value">]] Derived<int> doubled{[this] { return raw.get() * 2; }, raw};
};
}

TEST_CASE("tooltip in a tree: the annotation also works on a Derived member") {
    TreeFixture<Readouts> f;
    f.move(f.centre_of_widget_with("Twice the raw value"));
    f.wait_for_tooltip();
    CHECK(overlay_has(*f.snap, "Twice the raw value"));
}
#endif

#if __cpp_impl_reflection
namespace {
struct Mixer {
    [[=prism::core::tooltip<"Output level, in dB">]] Field<Slider<>> volume{{.value = 0.5}};
};
}

TEST_CASE("tooltip in a tree: the tooltip annotation on a reflected member") {
    TreeFixture<Mixer> f;
    f.move(f.centre_of_widget_with("Output level, in dB"));
    f.wait_for_tooltip();
    CHECK(overlay_has(*f.snap, "Output level, in dB"));
}
#endif

// --- in a running app: nothing but the timer wakes the logic thread ----------

#include <prism/app/headless_window.hpp>
#include <prism/app/model_app.hpp>

#include <condition_variable>
#include <mutex>

namespace {
// Hovers the last widget of the first frame, then waits -- sending nothing -- for a frame that
// draws `text`, and closes the window either way.
class HoverAndWaitBackend final : public prism::app::BackendBase {
    prism::app::HeadlessWindow window_{1, {}};
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<const SceneSnapshot>> frames_;
    std::string text_;

public:
    bool saw_tooltip = false;
    explicit HoverAndWaitBackend(std::string text) : text_(std::move(text)) {}

    prism::app::Window& create_window(prism::app::WindowConfig) override { return window_; }
    void submit(prism::app::WindowId, std::shared_ptr<const SceneSnapshot> s) override {
        { std::lock_guard l(m_); frames_.push_back(std::move(s)); }
        cv_.notify_all();
    }
    void wake() override {}
    void quit() override {}

    void run(std::function<void(const prism::app::WindowEvent&)> send) override {
        std::unique_lock l(m_);
        cv_.wait_for(l, 2s, [&] { return !frames_.empty(); });
        if (!frames_.empty()) {
            auto& [id, r] = frames_.front()->geometry.back();
            Point centre{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
            l.unlock();
            send({1, MouseMove{centre}});
            l.lock();
            saw_tooltip = cv_.wait_for(l, 5s, [&] {
                return std::ranges::any_of(frames_, [&](auto& f) { return overlay_has(*f, text_); });
            });
        }
        l.unlock();
        send({1, prism::app::WindowClose{}});
    }
};

struct OneButton {
    Field<Button> save{{"Save"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(save).tooltip("Write the file to disk"); }
};
}

TEST_CASE("tooltip in a running app: it appears after the delay with no further input") {
    OneButton model;
    auto impl = std::make_unique<HoverAndWaitBackend>("Write the file to disk");
    auto* probe = impl.get();
    prism::app::Backend backend{std::move(impl)};
    auto& window = backend.create_window({});
    prism::app::model_app(backend, window, model);
    CHECK(probe->saw_tooltip);
}
