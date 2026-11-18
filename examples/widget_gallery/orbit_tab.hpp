// Orbit: a small game drawn on a canvas. Drag back from the probe and let go; the planets and moons
// bend its path into the ring. It shows what a canvas can do beyond a chart: input (aim with a
// drag), animation (the flight and the following camera run on the app's AnimationClock, which
// ticks only while something moves), and fields steering it live (the gravity slider changes a path
// in flight). The rules are plain functions with tests of their own: orbit_physics.hpp, the
// hand-made levels in orbit_levels.hpp, random scenes in orbit_scene.hpp, the camera in
// orbit_camera.hpp and the stars, preview and fading in orbit_game.hpp. The drawing is
// orbit_canvas.cpp.
#pragma once

#include "orbit.hpp"
#include "orbit_camera.hpp"
#include "orbit_game.hpp"
#include "orbit_scene.hpp"

#include <prism/prism.hpp>

#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace gallery {

using namespace prism;

struct OrbitTab {
    core::Field<ui::Label<>> title{{.value = "Orbit", .heading = true}};
    core::Field<ui::Label<>> intro{{.value = "Drag back from the probe and let go: planets and moons bend its "
                                             "path into the ring. Move the sliders while it flies.",
                                    .wrap = true}};
    core::Field<ui::Choice> pick{{.placeholder = "Level"}};
    core::Field<ui::Label<>> difficulty_label{{.value = "Difficulty"}};
    core::Field<ui::SpinBox<int>> difficulty{{.value = 2, .min = 1, .max = 5, .step = 1}};
    core::Field<ui::Button> new_scene{{.text = "New scene", .primary = false}};
    core::Field<ui::Slider<>> gravity{{.value = 1.0, .min = 0.2, .max = 2.0, .step = 0.05, .label = "Gravity"}};
    core::Field<ui::Slider<>> persistence{{.value = 5, .min = 0, .max = 20, .step = 1, .label = "Persistence"}};
    core::Field<ui::Button> restart{{.text = "Restart", .primary = false}};
    core::Field<bool> random_scene{false};   // the difficulty only matters then
    core::Field<std::string> scene_name{"Level 1"};
    core::Field<int> launches{0};
    core::Field<std::string> message{"Aim with a drag."};
    // Computed from the fields it reads, with no code to keep it current.
    core::Derived<std::string> score{[this] {
        return std::format("{}   ·   {} launches   ·   {}", scene_name.get(), launches.get(), message.get());
    }};
    core::Field<int> frame{0};   // bumped to redraw the canvas
    core::Wiring wire;

    OrbitTab();
    void attach(app::AppContext& ctx);
    void view(app::ViewBuilder& vb);
    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node);
    void handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& node, core::Rect bounds);

private:
    void open(std::size_t index);   // a hand-made level, or the random scene past them
    void new_random_scene();
    void launch(orbit::Vec release);
    void ensure_clock();
    bool on_tick(ui::AnimationClock::time_point now);   // false once nothing moves any more
    void fly(double seconds);
    void land(orbit::Outcome outcome);
    void finish_win_pause(double seconds);
    void trim_old_trails();
    orbit::View current_view() const;   // the camera: eased while the clock runs, else the field
    orbit::View wanted_now() const;
    std::string landing_message(int stars, bool next_level) const;

    ui::AnimationClock* clock_ = nullptr;
    std::size_t index_ = 0;                           // the level shown; one past the last is random
    std::uint64_t seed_ = 0;                          // of the random scene
    orbit::Level level_;
    struct WinPause {
        double left;
        int stars;
    };
    std::optional<WinPause> win_pause_;               // the winning path stays up before the next level
    std::vector<int> best_;                           // the best stars per hand-made level, this session
    std::optional<orbit::World> world_;               // set while it flies
    std::optional<orbit::Vec> aim_;                   // where the drag is, in field units
    std::vector<orbit::Vec> trail_;                   // this flight's path
    std::deque<std::vector<orbit::Vec>> old_trails_;  // past flights, oldest first
    orbit::BodyTrails body_trails_;                   // the moving bodies' recent paths
    orbit::View view_{};                              // the camera while the clock runs
    double aspect_ = 1;                               // the canvas's shape, from the last draw
    bool clock_running_ = false;
    std::optional<ui::AnimationClock::time_point> last_tick_;
    double carry_ = 0;   // flight time not yet stepped (orbit::advance)
};

} // namespace gallery
