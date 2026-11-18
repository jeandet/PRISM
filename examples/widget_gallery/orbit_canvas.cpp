// Orbit's drawing: everything the canvas shows, through the camera. Stars and the grid keep one
// pixel size at any zoom; bodies, trails and the ring are drawn in field units and scale with it.
#include "orbit_screen.hpp"
#include "orbit_tab.hpp"

#include <algorithm>
#include <cmath>

namespace gallery {

using orbit::Vec;

namespace {
constexpr int star_count = 48;
constexpr float star_drift = 0.1f;        // how far the stars follow the camera: slower than the field
constexpr double grid_spacing = 100;      // field units
constexpr int trail_bands = 6;            // a body's trail fades in this many steps

const core::Color space = core::Color::rgba(14, 16, 28);
const core::Color star = core::Color::rgba(200, 205, 230, 120);
const core::Color grid = core::Color::rgba(160, 170, 210, 22);
const core::Color planet_fill = core::Color::rgba(196, 128, 72);
const core::Color planet_rim = core::Color::rgba(240, 180, 120);
const core::Color moon_fill = core::Color::rgba(110, 140, 190);
const core::Color moon_rim = core::Color::rgba(170, 200, 240);
const core::Color probe_color = core::Color::rgba(245, 245, 250);

core::Color with_alpha(core::Color c, double share) {
    return core::Color::rgba(c.r, c.g, c.b, static_cast<std::uint8_t>(std::clamp(c.a * share, 0.0, 255.0)));
}

// A number in [0, 1) from `n`, the same every time: a star's place needs no generator.
float unit_hash(std::uint32_t n) {
    n ^= n >> 16;
    n *= 0x45d9f3bu;
    n ^= n >> 16;
    n *= 0x45d9f3bu;
    return static_cast<float>(n & 0xffffu) / 65536.f;
}

float wrapped(float v, float size) { return v - size * std::floor(v / size); }

void draw_stars(render::DrawList& dl, core::Rect bounds, const orbit::View& view, float scale) {
    const float w = bounds.extent.w.raw(), h = bounds.extent.h.raw();
    for (std::uint32_t i = 0; i < star_count; ++i) {
        const float x = wrapped(unit_hash(2 * i) * w - static_cast<float>(view.centre.x) * scale * star_drift, w);
        const float y = wrapped(unit_hash(2 * i + 1) * h - static_cast<float>(view.centre.y) * scale * star_drift, h);
        dl.circle({core::X{x}, core::Y{y}}, 1.5f, star);
    }
}

void draw_grid(render::DrawList& dl, core::Rect bounds, const OrbitScreen& screen) {
    const Vec low = screen.to_field({core::X{0}, core::Y{0}});
    const Vec high = screen.to_field({core::X{bounds.extent.w.raw()}, core::Y{bounds.extent.h.raw()}});
    const auto first = [](double v) { return std::ceil(v / grid_spacing) * grid_spacing; };
    for (double x = first(low.x); x <= high.x; x += grid_spacing)
        dl.line(screen.to_canvas({x, low.y}), screen.to_canvas({x, high.y}), grid, 1.f);
    for (double y = first(low.y); y <= high.y; y += grid_spacing)
        dl.line(screen.to_canvas({low.x, y}), screen.to_canvas({high.x, y}), grid, 1.f);
}

void draw_path(render::DrawList& dl, const OrbitScreen& screen, const auto& points, core::Color color, float width) {
    if (points.size() < 2) return;
    std::vector<core::Point> out;
    out.reserve(points.size());
    for (const auto& p : points) out.push_back(screen.to_canvas(p));
    dl.polyline(std::move(out), color, width);
}

// A body's trail in a few bands, the oldest the faintest.
void draw_fading_path(render::DrawList& dl, const OrbitScreen& screen, const std::vector<Vec>& points, core::Color color) {
    if (points.size() < 2) return;
    const std::size_t band = std::max<std::size_t>(points.size() / trail_bands, 2);
    const std::size_t count = orbit::trail_band_count(points.size(), band);
    for (std::size_t from = 0, rank = 0; from + 1 < points.size(); from += band, ++rank) {
        const std::size_t to = std::min(from + band + 1, points.size());
        draw_path(dl, screen, std::vector<Vec>(points.begin() + static_cast<std::ptrdiff_t>(from),
                                               points.begin() + static_cast<std::ptrdiff_t>(to)),
                  with_alpha(color, orbit::fade(count - 1 - rank, count)), 1.5f);
    }
}

void draw_body_trails(render::DrawList& dl, const OrbitScreen& screen, const orbit::BodyTrails& trails, core::Color color) {
    for (const auto& trail : trails) {
        std::vector<Vec> points;
        for (const auto& p : trail) points.push_back(p.pos);
        draw_fading_path(dl, screen, points, color);
    }
}

void draw_bodies(render::DrawList& dl, const OrbitScreen& screen, const std::vector<orbit::Body>& bodies) {
    for (const auto& b : bodies) {
        if (!b.alive) continue;
        dl.circle(screen.to_canvas(b.pos), screen.length(b.radius), b.anchor ? planet_fill : moon_fill);
        dl.circle(screen.to_canvas(b.pos), screen.length(b.radius), b.anchor ? planet_rim : moon_rim, 2.f);
    }
}

void draw_ring(render::DrawList& dl, const OrbitScreen& screen, Vec at, double radius, core::Color color) {
    dl.circle(screen.to_canvas(at), screen.length(radius), color, 3.f);
    dl.circle(screen.to_canvas(at), screen.length(radius * 0.35), color);
}

// The band pulled back, and the dotted path the launch would take.
void draw_aim(render::DrawList& dl, const OrbitScreen& screen, const std::vector<Vec>& preview, Vec start, Vec aim,
              core::Color accent) {
    dl.line(screen.to_canvas(start), screen.to_canvas(aim), core::Color::rgba(255, 255, 255, 90), 1.5f);
    for (Vec p : preview) dl.circle(screen.to_canvas(p), 2.f, accent);
}
} // namespace

void OrbitTab::canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
    aspect_ = OrbitScreen::aspect(bounds);
    const orbit::View view = current_view();
    const OrbitScreen screen{bounds, view};
    const auto& accent = node.theme->accent;
    const core::Color accent_color = core::Color::rgba(accent.r, accent.g, accent.b);

    dl.filled_rect(bounds, space);
    draw_stars(dl, bounds, view, screen.scale);
    draw_grid(dl, bounds, screen);

    for (std::size_t i = 0; i < old_trails_.size(); ++i)
        draw_path(dl, screen, old_trails_[i], with_alpha(core::Color::rgba(accent.r, accent.g, accent.b, 160),
                                                          orbit::fade(old_trails_.size() - 1 - i, old_trails_.size())), 1.5f);
    draw_path(dl, screen, trail_, accent_color, 2.f);
    draw_body_trails(dl, screen, body_trails_, moon_rim);

    const orbit::World idle = world_ ? orbit::World{} : orbit::launch(level_, {}, value(gravity));
    const orbit::World& shown = world_ ? *world_ : idle;
    draw_bodies(dl, screen, shown.bodies);
    const Vec ring = orbit::ring_centre(shown, level_);
    draw_ring(dl, screen, ring, level_.target_radius, accent_color);

    if (aim_)
        draw_aim(dl, screen, orbit::preview_path(level_, orbit::launch_velocity(level_.start, *aim_), value(gravity)),
                 level_.start, *aim_, accent_color);
    dl.circle(screen.to_canvas(shown.probe.pos), std::max(screen.length(8), 3.f), probe_color);
}

} // namespace gallery
