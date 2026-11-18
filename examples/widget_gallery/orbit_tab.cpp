#include "orbit_tab.hpp"

#include "orbit_screen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace gallery {

using orbit::Vec;

namespace {
constexpr double grab_radius = 60;   // how near the probe a drag must start, in field units
constexpr double settled_distance = 0.5;   // field units; the camera this near its goal stops
constexpr double longest_tick = 0.05;      // a long pause is not a long jump
constexpr double win_pause_seconds = 1.5;  // how long a win shows its path before the next level

std::size_t level_count() { return orbit::levels().size(); }

std::vector<std::string> level_names() {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < level_count(); ++i) names.push_back(std::format("Level {}", i + 1));
    names.push_back("Random");
    return names;
}

std::uint64_t seed_from_clock() {
    return static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count()) % 100000;
}

bool near(const orbit::View& a, const orbit::View& b) {
    return orbit::length(a.centre - b.centre) < settled_distance && std::abs(a.width - b.width) < settled_distance;
}
} // namespace

OrbitTab::OrbitTab() : best_(level_count(), 0) {
    pick.update([](ui::Choice& c) {
        c.options = level_names();
        c.selected = 0;
    });
    wire.on(pick, [this](const ui::Choice& c) {
        if (c.selected && *c.selected != index_) open(*c.selected);
    });
    wire.on(new_scene, [this](const ui::Button&) { new_random_scene(); });
    wire.on(difficulty, [this](const ui::SpinBox<int>&) {
        if (index_ == level_count()) open(index_);
    });
    wire.on(restart, [this](const ui::Button&) {
        open(0);
        pick.update([](ui::Choice& c) { c.selected = 0; });
    });
    wire.on(gravity, [this](const ui::Slider<>&) { ++frame; });
    wire.on(persistence, [this](const ui::Slider<>&) {
        trim_old_trails();
        ++frame;
    });
    seed_ = seed_from_clock();
    open(0);
}

void OrbitTab::attach(app::AppContext& ctx) { clock_ = &ctx.clock(); }

// simplify: a random scene is generated on the UI thread. It takes ~20 ms on average and 0.3 s at
// worst natively; move it to a thread (Shared<Level>) if a click ever visibly stalls.
void OrbitTab::open(std::size_t index) {
    index_ = index;
    const bool random = index >= level_count();
    level_ = random ? orbit::random_level(seed_, value(difficulty)) : orbit::levels()[index];
    random_scene.set(random);
    win_pause_.reset();
    if (random) gravity.update([](ui::Slider<>& s) { s.value = 1.0; });   // scenes are proven at gravity 1
    scene_name = random ? std::format("Random scene #{} · difficulty {}", seed_, value(difficulty))
                        : std::format("Level {} of {}", index + 1, level_count());
    world_.reset();
    aim_.reset();
    trail_.clear();
    old_trails_.clear();
    body_trails_.clear();
    launches = 0;
    message = "Aim with a drag.";
    ++frame;
}

void OrbitTab::new_random_scene() {
    seed_ = seed_from_clock();
    open(level_count());
    pick.update([](ui::Choice& c) { c.selected = level_count(); });
}

orbit::View OrbitTab::wanted_now() const {
    return orbit::wanted_view(world_ ? std::optional<Vec>{world_->probe.pos} : std::nullopt, aspect_);
}

orbit::View OrbitTab::current_view() const {
    return clock_running_ ? view_ : orbit::wanted_view(std::nullopt, aspect_);
}

void OrbitTab::launch(Vec release) {
    world_ = orbit::launch(level_, orbit::launch_velocity(level_.start, release), value(gravity));
    trail_ = {level_.start};
    body_trails_.clear();
    ++launches;
    message = "Flying...";
    carry_ = 0;
    view_ = current_view();
    ensure_clock();
}

void OrbitTab::ensure_clock() {
    if (!clock_ || clock_running_) return;
    clock_running_ = true;
    last_tick_.reset();
    clock_->add([this](ui::AnimationClock::time_point now) { return on_tick(now); });
}

bool OrbitTab::on_tick(ui::AnimationClock::time_point now) {
    const double dt = last_tick_ ? std::min(std::chrono::duration<double>(now - *last_tick_).count(), longest_tick) : 0.0;
    last_tick_ = now;
    if (world_) fly(dt);
    if (win_pause_) finish_win_pause(dt);
    view_ = orbit::eased(view_, wanted_now(), dt);
    if (world_) view_ = orbit::contains(view_, world_->probe.pos, aspect_);
    ++frame;
    const bool going = world_ || win_pause_ || !near(view_, wanted_now());
    clock_running_ = going;
    return going;
}

void OrbitTab::fly(double seconds) {
    // gravity is read every tick: the slider steers a flight in progress
    const auto outcome = orbit::advance(*world_, level_, value(gravity), seconds, carry_);
    orbit::record_trails(body_trails_, *world_, orbit::trail_seconds(value(persistence)));
    // A point per few units, not per tick: the clock ticks far more often than the screen draws.
    if (trail_.empty() || orbit::length(world_->probe.pos - trail_.back()) > 4 || outcome != orbit::Outcome::Flying)
        trail_.push_back(world_->probe.pos);
    if (outcome != orbit::Outcome::Flying) land(outcome);
}

void OrbitTab::trim_old_trails() {
    const auto keep = static_cast<std::size_t>(value(persistence));
    while (old_trails_.size() > keep) old_trails_.pop_front();
}

std::string OrbitTab::landing_message(int stars, bool next_level) const {
    const bool hand_made = index_ < level_count();
    const auto best = hand_made ? std::format(" (best {})", best_[index_]) : std::string{};
    return std::format("In the ring! {} of 3 stars{}. {}", stars, best,
                       next_level ? "Next level." : hand_made ? "That was the last level." : "Press New scene for another.");
}

void OrbitTab::finish_win_pause(double seconds) {
    win_pause_->left -= seconds;
    if (win_pause_->left > 0) return;
    const int stars = win_pause_->stars;
    const std::size_t next = index_ + 1;
    open(next);
    pick.update([next](ui::Choice& c) { c.selected = next; });
    message = landing_message(stars, true);
}

void OrbitTab::land(orbit::Outcome outcome) {
    const bool hit = outcome == orbit::Outcome::Hit;
    const bool pausing = hit && index_ + 1 < level_count() && clock_;
    world_.reset();
    body_trails_.clear();
    if (!pausing) {
        old_trails_.push_back(std::move(trail_));
        trim_old_trails();
        trail_.clear();
    }
    if (!hit) {
        message = outcome == orbit::Outcome::Crashed ? "Crashed. Try again." : "Lost in space. Try again.";
        return;
    }
    const int stars = orbit::stars_for(launches.get());
    if (index_ < level_count()) best_[index_] = std::max(best_[index_], stars);
    if (pausing) {
        win_pause_ = WinPause{.left = win_pause_seconds, .stars = stars};
        message = std::format("Hit! {} of 3 stars", stars);
        return;
    }
    message = landing_message(stars, false);
}

void OrbitTab::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& node, core::Rect bounds) {
    aspect_ = OrbitScreen::aspect(bounds);
    const OrbitScreen screen{bounds, current_view()};
    if (const auto* mb = std::get_if<input::MouseButton>(&ev); mb && mb->button == input::buttons::left) {
        if (mb->pressed) {
            const Vec at = screen.to_field(mb->position);
            if (!world_ && !win_pause_ && orbit::length(at - level_.start) < grab_radius) aim_ = at;
        } else if (aim_) {
            const Vec release = *aim_;
            aim_.reset();
            launch(release);
        }
        ++frame;
    } else if (const auto* mm = std::get_if<input::MouseMove>(&ev); mm && aim_ && ui::node_vs(node).pressed) {
        aim_ = screen.to_field(mm->position);
        ++frame;
    }
}

void OrbitTab::view(app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(intro);
    vb.hstack([&] {
        vb.widget(pick).tooltip("A hand-made level, or a random scene");
        vb.gap(8.f);
        vb.widget(difficulty_label).enabled(random_scene);
        vb.gap(4.f);
        vb.widget(difficulty).enabled(random_scene).tooltip("Difficulty of random scenes, 1 to 5");
        vb.gap(8.f);
        vb.widget(new_scene);
        vb.spacer();
    });
    vb.canvas(*this).depends_on(frame).min_size(core::Height{360});
    vb.widget(score);
    vb.hstack([&] {
        vb.widget(gravity).tooltip("Changes the pull of every body, even mid-flight");
        vb.gap(16.f);
        vb.widget(persistence).tooltip("How many past flights stay drawn, and how long moving bodies leave a trail");
        vb.gap(16.f);
        vb.widget(restart);
    });
}

} // namespace gallery
