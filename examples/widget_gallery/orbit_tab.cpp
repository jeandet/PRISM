#include "orbit_tab.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace gallery {

using orbit::Vec;

namespace {
constexpr double grab_radius = 60;           // how near the probe a drag must start, in field units
constexpr std::size_t kept_trails = 5;

// A few fixed stars, the same at every size.
constexpr Vec stars[] = {{60, 40}, {210, 520}, {330, 90}, {470, 560}, {610, 60}, {760, 540},
                         {880, 70}, {950, 420}, {140, 250}, {420, 420}, {700, 300}, {980, 200}};

const core::Color space = core::Color::rgba(14, 16, 28);
const core::Color star = core::Color::rgba(200, 205, 230, 120);
const core::Color planet_fill = core::Color::rgba(196, 128, 72);
const core::Color planet_rim = core::Color::rgba(240, 180, 120);
const core::Color probe_color = core::Color::rgba(245, 245, 250);
} // namespace

// The field keeps its shape in any canvas: scaled to fit, centered.
struct OrbitTab::Transform {
    float scale, dx, dy;
    explicit Transform(core::Rect bounds) {
        scale = static_cast<float>(std::min(bounds.extent.w.raw() / orbit::field_width,
                                            bounds.extent.h.raw() / orbit::field_height));
        dx = (bounds.extent.w.raw() - scale * static_cast<float>(orbit::field_width)) / 2;
        dy = (bounds.extent.h.raw() - scale * static_cast<float>(orbit::field_height)) / 2;
    }
    core::Point to_canvas(Vec v) const {
        return {core::X{dx + scale * static_cast<float>(v.x)}, core::Y{dy + scale * static_cast<float>(v.y)}};
    }
    Vec to_field(core::Point p) const {
        return {(p.x.raw() - dx) / scale, (p.y.raw() - dy) / scale};
    }
    float length(double l) const { return scale * static_cast<float>(l); }
};

OrbitTab::OrbitTab() {
    wire.on(restart, [this](const ui::Button&) {
        level = 0;
        attempts = 0;
        old_trails_.clear();
        reset_level();
    });
    wire.on(gravity, [this](const ui::Slider<>&) { ++frame; });
}

void OrbitTab::attach(app::AppContext& ctx) { clock_ = &ctx.clock(); }

const orbit::Level& OrbitTab::current() const {
    const auto& all = orbit::levels();
    return all[static_cast<std::size_t>(level.get()) % all.size()];
}

void OrbitTab::reset_level() {
    world_.reset();
    aim_.reset();
    trail_.clear();
    message = "Aim with a drag.";
    ++frame;
}

void OrbitTab::launch(Vec release) {
    const auto& lv = current();
    world_ = orbit::launch(lv, orbit::launch_velocity(lv.start, release));
    trail_ = {lv.start};
    ++attempts;
    message = "Flying...";
    last_tick_.reset();
    carry_ = 0;
    if (clock_)
        clock_->add([this](ui::AnimationClock::time_point now) {
            double dt = last_tick_ ? std::chrono::duration<double>(now - *last_tick_).count() : 0.0;
            last_tick_ = now;
            return tick(std::min(dt, 0.05));   // a long pause is not a long jump
        });
}

bool OrbitTab::tick(double seconds) {
    if (!world_) return false;
    const auto& lv = current();
    // gravity is read every tick: the slider steers a flight in progress
    if (auto outcome = orbit::advance(*world_, lv, value(gravity), seconds, carry_); outcome != orbit::Outcome::Flying) {
        trail_.push_back(world_->probe.pos);
        land(outcome);
        return false;
    }
    // A point per few units, not per tick: the clock ticks far more often than the screen draws.
    if (trail_.empty() || orbit::length(world_->probe.pos - trail_.back()) > 4) {
        trail_.push_back(world_->probe.pos);
        ++frame;
    }
    return true;
}

void OrbitTab::land(orbit::Outcome outcome) {
    old_trails_.push_back(std::move(trail_));
    if (old_trails_.size() > kept_trails) old_trails_.erase(old_trails_.begin());
    world_.reset();
    trail_.clear();
    if (outcome == orbit::Outcome::Hit) {
        const bool last = level.get() + 1 == static_cast<int>(orbit::levels().size());
        message = last ? std::format("All done in {} launches!", attempts.get()) : "In the ring! Next level.";
        if (!last) {
            ++level;
            old_trails_.clear();
        }
    } else {
        message = outcome == orbit::Outcome::Crashed ? "Crashed. Try again." : "Lost in space. Try again.";
    }
    ++frame;
}

void OrbitTab::handle_canvas_input(const input::InputEvent& ev, ui::WidgetNode& node, core::Rect bounds) {
    const Transform tf{bounds};
    const Vec start = current().start;
    if (const auto* mb = std::get_if<input::MouseButton>(&ev); mb && mb->button == input::buttons::left) {
        if (mb->pressed) {
            Vec at = tf.to_field(mb->position);
            if (!world_ && orbit::length(at - start) < grab_radius) aim_ = at;
        } else if (aim_) {
            Vec release = *aim_;
            aim_.reset();
            launch(release);
        }
        ++frame;
    } else if (const auto* mm = std::get_if<input::MouseMove>(&ev); mm && aim_ && ui::node_vs(node).pressed) {
        aim_ = tf.to_field(mm->position);
        ++frame;
    }
}

void OrbitTab::canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
    const Transform tf{bounds};
    const auto& lv = current();
    const auto& theme = *node.theme;
    dl.filled_rect(bounds, space);
    for (Vec s : stars) dl.circle(tf.to_canvas(s), 1.5f, star);

    auto path = [&](const std::vector<Vec>& pts, core::Color c, float width) {
        if (pts.size() < 2) return;
        std::vector<core::Point> out;
        out.reserve(pts.size());
        for (Vec p : pts) out.push_back(tf.to_canvas(p));
        dl.polyline(std::move(out), c, width);
    };
    for (const auto& t : old_trails_) path(t, core::Color::rgba(theme.accent.r, theme.accent.g, theme.accent.b, 70), 1.5f);
    path(trail_, theme.accent, 2.f);

    const auto& bodies = world_ ? world_->bodies : lv.bodies;
    for (const auto& b : bodies) {
        dl.circle(tf.to_canvas(b.pos), tf.length(b.radius), planet_fill);
        dl.circle(tf.to_canvas(b.pos), tf.length(b.radius), planet_rim, 2.f);
    }
    dl.circle(tf.to_canvas(lv.target), tf.length(lv.target_radius), theme.accent, 3.f);
    dl.circle(tf.to_canvas(lv.target), tf.length(lv.target_radius * 0.35), theme.accent);

    const Vec probe_at = world_ ? world_->probe.pos : lv.start;
    if (aim_) {   // the band pulled back, and the way the probe will go
        dl.line(tf.to_canvas(lv.start), tf.to_canvas(*aim_), core::Color::rgba(255, 255, 255, 90), 1.5f);
        Vec v = orbit::launch_velocity(lv.start, *aim_);
        dl.line(tf.to_canvas(lv.start), tf.to_canvas(lv.start + v * 0.25), theme.accent, 3.f);
    }
    dl.circle(tf.to_canvas(probe_at), std::max(tf.length(8), 3.f), probe_color);
}

void OrbitTab::view(app::ViewBuilder& vb) {
    vb.widget(title);
    vb.widget(intro);
    vb.canvas(*this).depends_on(frame, level).min_size(core::Height{360});
    vb.widget(score);
    vb.hstack([&] {
        vb.widget(gravity).tooltip("Changes the pull of every planet, even mid-flight");
        vb.gap(16.f);
        vb.widget(restart);
    });
}

} // namespace gallery
