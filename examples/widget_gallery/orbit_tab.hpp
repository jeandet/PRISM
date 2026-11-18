// Orbit: a small game drawn on a canvas. Drag back from the probe and let go; the planets bend
// its path into the ring. It shows what a canvas can do beyond a chart: input (aim with a drag),
// animation (the flight runs on the app's clock, which ticks only while the probe flies), and
// fields steering it live (the gravity slider changes a path in flight). The physics is
// orbit_physics.hpp, plain functions with tests of their own; the levels are orbit_levels.hpp.
#pragma once

#include "orbit.hpp"

#include <prism/prism.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

namespace gallery {

using namespace prism;

struct OrbitTab {
    core::Field<ui::Label<>> title{{.value = "Orbit", .heading = true}};
    core::Field<ui::Label<>> intro{{.value = "Drag back from the probe and let go: the planets bend its "
                                             "path into the ring. Move the slider while it flies.",
                                    .wrap = true}};
    core::Field<ui::Slider<>> gravity{{.value = 1.0, .min = 0.2, .max = 2.0, .step = 0.05, .label = "Gravity"}};
    core::Field<ui::Button> restart{{.text = "Restart", .primary = false}};
    core::Field<int> level{0};
    core::Field<int> attempts{0};
    core::Field<std::string> message{"Aim with a drag."};
    // Computed from the fields it reads, with no code to keep it current.
    core::Derived<std::string> score{[this] {
        return std::format("Level {} of {}   ·   {} launches   ·   {}", level.get() + 1,
                           orbit::levels().size(), attempts.get(), message.get());
    }};
    core::Field<int> frame{0};   // bumped to redraw the canvas
    core::Wiring wire;

    OrbitTab();
    void attach(app::AppContext& ctx);
    void view(app::ViewBuilder& vb);
    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node);
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& node, core::Rect bounds);

private:
    struct Transform;   // field units to the canvas and back (orbit_tab.cpp)

    const orbit::Level& current() const;
    void launch(orbit::Vec release);
    bool tick(double seconds);   // false once the probe stopped flying
    void land(orbit::Outcome outcome);
    void reset_level();

    ui::AnimationClock* clock_ = nullptr;
    std::optional<orbit::World> world_;              // set while it flies
    std::optional<orbit::Vec> aim_;                  // where the drag is, in field units
    std::vector<orbit::Vec> trail_;                  // this flight's path
    std::vector<std::vector<orbit::Vec>> old_trails_;   // the last few flights, faded
    std::optional<ui::AnimationClock::time_point> last_tick_;
    double carry_ = 0;   // flight time not yet stepped (orbit::advance)
};

} // namespace gallery
