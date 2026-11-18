// The gallery's Orbit game: its physics is plain functions over plain values, tested here with
// no window.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/widget_gallery/orbit.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace orbit;

namespace {
constexpr double step_s = 1.0 / 240.0;   // the fixed step every flight is made of

// Steps until the probe stops flying, or gives up after `max_steps`.
Outcome fly(World& w, const Level& level, double gravity, int max_steps = 100'000) {
    for (int i = 0; i < max_steps; ++i)
        if (auto outcome = step(w, level, gravity, step_s); outcome != Outcome::Flying) return outcome;
    return Outcome::Flying;
}

// `count` steps of the fixed step, whatever became of the probe (for watching the bodies).
void run(World& w, const Level& level, double gravity, int count) {
    for (int i = 0; i < count; ++i) step(w, level, gravity, step_s);
}

Level empty_level() { return {.start = {100, 300}, .target = {900, 300}, .target_radius = 30, .bodies = {}}; }
} // namespace

TEST_CASE("with no body a probe flies straight") {
    auto level = empty_level();
    level.target = {500, 100};   // out of its way
    World w = launch(level, {200, 0});
    step(w, level, 1.0, 0.5);
    CHECK(w.probe.pos.x == doctest::Approx(200));
    CHECK(w.probe.pos.y == doctest::Approx(300));
}

TEST_CASE("a probe aimed at the target with no body reaches it") {
    auto level = empty_level();
    World w = launch(level, {400, 0});
    CHECK(fly(w, level, 1.0) == Outcome::Hit);
}

TEST_CASE("a probe that leaves the field is lost") {
    auto level = empty_level();
    World w = launch(level, {-400, 0});
    CHECK(fly(w, level, 1.0) == Outcome::Lost);
}

TEST_CASE("a probe aimed straight at a planet crashes into it") {
    Level level = empty_level();
    level.bodies = {{.pos = {500, 300}, .mass = 1.0, .radius = 40}};
    World w = launch(level, {300, 0});
    CHECK(fly(w, level, 1.0) == Outcome::Crashed);
}

TEST_CASE("a planet pulls a passing probe toward it") {
    Level level = empty_level();
    level.start = {300, 300};
    level.bodies = {{.pos = {500, 200}, .mass = 1.0, .radius = 20}};
    level.target = {500, 900};   // out of reach
    World w = launch(level, {300, 0});
    run(w, level, 1.0, 240);
    CHECK(w.probe.vel.y < 0);   // bent up, toward the planet above
}

TEST_CASE("stronger gravity bends the path more") {
    Level level = empty_level();
    level.start = {300, 300};
    level.bodies = {{.pos = {500, 200}, .mass = 1.0, .radius = 20}};
    level.target = {500, 900};
    World weak = launch(level, {300, 0}), strong = launch(level, {300, 0});
    for (int i = 0; i < 240; ++i) {
        step(weak, level, 0.5, step_s);
        step(strong, level, 2.0, step_s);
    }
    CHECK(strong.probe.vel.y < weak.probe.vel.y);
}

TEST_CASE("a launch is the drag, reversed and scaled, capped at the top speed") {
    Vec v = launch_velocity({100, 300}, {60, 330});   // pulled back down-left
    CHECK(v.x > 0);
    CHECK(v.y < 0);
    Vec fast = launch_velocity({100, 300}, {-5000, 300});
    CHECK(std::hypot(fast.x, fast.y) == doctest::Approx(max_launch_speed));
}

TEST_CASE("a launch copies the level: its bodies as they start, the probe at its start") {
    const Level& level = levels().front();
    World w = launch(level, {120, 0});
    REQUIRE(w.bodies.size() == level.bodies.size());
    for (std::size_t i = 0; i < level.bodies.size(); ++i) {
        CHECK(w.bodies[i].pos.x == level.bodies[i].pos.x);
        CHECK(w.bodies[i].pos.y == level.bodies[i].pos.y);
    }
    CHECK(w.probe.pos.x == level.start.x);
    CHECK(w.probe.pos.y == level.start.y);
    CHECK(w.probe.vel.x == 120);
    CHECK(w.time == 0);
}

TEST_CASE("every level's ring is in the field and not buried in a body") {
    for (const auto& level : levels()) {
        World w = launch(level, {0, 0});
        Vec ring = ring_centre(w, level);
        CHECK(ring.x > 0);
        CHECK(ring.x < field_width);
        CHECK(ring.y > 0);
        CHECK(ring.y < field_height);
        if (level.target_body) continue;   // a carried ring rides inside its body by design
        for (const auto& body : level.bodies) CHECK(length(body.pos - ring) > body.radius + level.target_radius);
    }
}

TEST_CASE("every level has a winning launch at the default gravity") {
    for (const auto& level : levels()) {
        bool won = false;
        for (int a = 0; a < 360 && !won; a += 2)
            for (double pull = 20; pull <= 180 && !won; pull += 10) {
                double angle = a * 3.14159265358979 / 180;
                Vec release = level.start + Vec{std::cos(angle), std::sin(angle)} * pull;
                World w = launch(level, launch_velocity(level.start, release));
                won = fly(w, level, 1.0, 240 * 20) == Outcome::Hit;
            }
        CHECK(won);
    }
}

TEST_CASE("many short ticks fly as far as the time they add up to, not one step each") {
    auto level = empty_level();
    level.target = {500, 100};   // out of its way
    World w = launch(level, {200, 0});
    double carry = 0;
    for (int i = 0; i < 100; ++i) advance(w, level, 1.0, 0.001, carry);   // 0.1 s in all
    CHECK(w.probe.pos.x == doctest::Approx(120).epsilon(0.01));   // 200 units/s for 0.1 s
}

TEST_CASE("a moon placed by the level helper stays in orbit for a minute") {
    Level level = empty_level();
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back(orbiting(level.bodies, 0, 200, 0.0, 1.0, 12));
    World w = launch(level, {0, 0});
    run(w, level, 1.0, 60 * 240);
    REQUIRE(w.bodies.size() == 2);
    CHECK(length(w.bodies[1].pos - w.bodies[0].pos) == doctest::Approx(200).epsilon(0.10));
}

TEST_CASE("the moons' order in the bodies vector does not change where they end up") {
    Level level = empty_level();
    level.target = {900, 900};   // out of the way
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back(orbiting(level.bodies, 0, 200, 0.0, 0.15, 10));
    level.bodies.push_back(
        orbiting(level.bodies, 0, 200, 0.5, 0.15, 10));   // opposite the first, near enough to pull on it
    Level swapped = level;
    std::swap(swapped.bodies[1], swapped.bodies[2]);
    World a = launch(level, {0, 0}), b = launch(swapped, {0, 0});
    run(a, level, 1.0, 10 * 240);   // ten seconds
    run(b, swapped, 1.0, 10 * 240);
    REQUIRE(a.bodies.size() == 3);
    REQUIRE(b.bodies.size() == 3);
    REQUIRE(length(a.bodies[1].pos - level.bodies[1].pos) > 1);   // the moons really flew, and pulled on each other
    for (std::size_t i = 1; i <= 2; ++i) {
        const std::size_t j = 3 - i;   // the same moon's slot in the swapped world
        CHECK(std::abs(a.bodies[i].pos.x - b.bodies[j].pos.x) < 1e-9);
        CHECK(std::abs(a.bodies[i].pos.y - b.bodies[j].pos.y) < 1e-9);
        CHECK(std::abs(a.bodies[i].vel.x - b.bodies[j].vel.x) < 1e-9);
        CHECK(std::abs(a.bodies[i].vel.y - b.bodies[j].vel.y) < 1e-9);
    }
}

TEST_CASE("the same launch twice flies the same way") {
    const Level& level = levels().front();
    Vec v = launch_velocity(level.start, level.start + Vec{-120, 40});
    World a = launch(level, v), b = launch(level, v);
    const Outcome first = fly(a, level, 1.0), second = fly(b, level, 1.0);
    CHECK(first == second);
    CHECK(a.time > 0.5);   // it flew, rather than standing still
    CHECK(a.probe.pos.x == b.probe.pos.x);
    CHECK(a.probe.pos.y == b.probe.pos.y);
}

TEST_CASE("the gravity slider steers a flight in progress") {
    Level level = empty_level();
    level.bodies = {{.pos = {800, 200}, .mass = 1.0, .radius = 20}};
    level.target = {500, 900};   // out of the way
    World raised = launch(level, {300, 0}), steady = launch(level, {300, 0});
    for (int i = 0; i < 240; ++i) {
        step(raised, level, i < 120 ? 1.0 : 2.0, step_s);
        step(steady, level, 1.0, step_s);
    }
    CHECK(raised.probe.pos.y < steady.probe.pos.y);   // the stronger second half lifted it further
    CHECK(raised.probe.vel.y < steady.probe.vel.y);
}

TEST_CASE("a retry starts from the level's state, and the next launch at that gravity replays") {
    Level level = empty_level();
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back(orbiting(level.bodies, 0, 200, 0.0, 1.0, 12));
    Vec v = launch_velocity(level.start, level.start + Vec{-100, 30});
    World steered = launch(level, v);
    run(steered, level, 1.0, 120);
    CHECK(length(steered.bodies[1].pos - level.bodies[1].pos) > 1);   // the moon moved during that flight

    World retry = launch(level, v);
    REQUIRE(retry.bodies.size() == level.bodies.size());
    for (std::size_t i = 0; i < level.bodies.size(); ++i) {
        CHECK(retry.bodies[i].pos.x == level.bodies[i].pos.x);
        CHECK(retry.bodies[i].pos.y == level.bodies[i].pos.y);
    }
    CHECK(retry.time == 0);

    World a = launch(level, v), b = launch(level, v);
    const Outcome first = fly(a, level, 2.0), second = fly(b, level, 2.0);
    CHECK(first == second);
    CHECK(a.time > 0.5);
    CHECK(a.probe.pos.x == b.probe.pos.x);
    CHECK(a.probe.pos.y == b.probe.pos.y);
}

TEST_CASE("a moon that hits an anchor dies where it is, and the anchor stays") {
    Level level = empty_level();
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back({.pos = {380, 300}, .vel = {200, 0}, .mass = 0.5, .radius = 10, .anchor = false});
    World w = launch(level, {0, 0});
    for (int i = 0; i < 240 && w.bodies[1].alive; ++i) step(w, level, 1.0, step_s);
    REQUIRE(w.bodies.size() == 2);   // the dead moon keeps its slot, so the anchor keeps index 0
    CHECK_FALSE(w.bodies[1].alive);
    CHECK(w.bodies[0].anchor);
    CHECK(w.bodies[0].pos.x == doctest::Approx(500));
}

TEST_CASE("two moons that hit each other both die") {
    Level level = empty_level();
    level.bodies.push_back({.pos = {450, 300}, .vel = {60, 0}, .mass = 0.5, .radius = 15, .anchor = false});
    level.bodies.push_back({.pos = {550, 300}, .vel = {-60, 0}, .mass = 0.5, .radius = 15, .anchor = false});
    World w = launch(level, {0, 0});
    for (int i = 0; i < 240 && (w.bodies[0].alive || w.bodies[1].alive); ++i) step(w, level, 1.0, step_s);
    CHECK_FALSE(w.bodies[0].alive);
    CHECK_FALSE(w.bodies[1].alive);
}

TEST_CASE("a probe that crosses a moon crashes") {
    Level level = empty_level();
    level.target = {900, 900};   // out of the way
    level.bodies.push_back({.pos = {500, 300}, .mass = 0.5, .radius = 40, .anchor = false});
    World w = launch(level, {300, 0});
    CHECK(fly(w, level, 1.0) == Outcome::Crashed);
}

TEST_CASE("a probe further than 1.5 diagonals from the field's centre is lost") {
    auto level = empty_level();
    level.target = {100, 560};   // out of the way
    World w = launch(level, {400, 0});
    CHECK(fly(w, level, 1.0) == Outcome::Lost);
    CHECK(length(w.probe.pos - field_centre()) > 1.5 * field_diagonal());
}

TEST_CASE("a probe still flying after thirty seconds is lost") {
    auto level = empty_level();
    level.target = {900, 900};   // out of the way
    World w = launch(level, {10, 0});   // a slow drift, well inside the field
    double carry = 0;
    CHECK(advance(w, level, 1.0, 31.0, carry) == Outcome::Lost);
    CHECK(w.time > 30);
    CHECK(w.time < 31);
}

TEST_CASE("a ring carried by a moon follows it") {
    Level level = empty_level();
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back(orbiting(level.bodies, 0, 200, 0.0, 1.0, 12));
    level.target_body = 1;
    level.target_radius = 12;
    World w = launch(level, {0, 0});
    Vec first = ring_centre(w, level);
    CHECK(first.x == w.bodies[1].pos.x);
    CHECK(first.y == w.bodies[1].pos.y);
    run(w, level, 1.0, 240);
    Vec later = ring_centre(w, level);
    CHECK(later.x == w.bodies[1].pos.x);
    CHECK(later.y == w.bodies[1].pos.y);
    CHECK(later.x != first.x);   // it rode the moon instead of staying put
}

TEST_CASE("a ring with no body carrying it stays where the level puts it") {
    auto level = empty_level();
    World w = launch(level, {0, 0});
    CHECK(ring_centre(w, level).x == level.target.x);
    CHECK(ring_centre(w, level).y == level.target.y);
}

TEST_CASE("a body removed before the carrier leaves the ring on its carrier") {
    Level level = empty_level();
    level.target = {100, 560};   // the ring's fallback point, well away from the carrier
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back({.pos = {300, 300}, .vel = {200, 0}, .mass = 0.5, .radius = 10, .anchor = false});
    level.bodies.push_back(orbiting(level.bodies, 0, 200, 0.25, 0.5, 12));   // the carrier
    level.target_body = 2;

    World w = launch(level, {0, 0});
    for (int i = 0; i < 240 && w.bodies.size() == 3 && w.bodies[1].alive; ++i)
        step(w, level, 1.0, step_s);
    REQUIRE(w.bodies.size() == 3);   // the removed body stays in place, so the carrier keeps index 2
    CHECK_FALSE(w.bodies[1].alive);
    CHECK(w.bodies[2].alive);

    const Vec where = ring_centre(w, level);
    CHECK(where.x == w.bodies[2].pos.x);
    CHECK(where.y == w.bodies[2].pos.y);
    CHECK(where.x != level.target.x);   // not the ring's fallback point

    run(w, level, 1.0, 240);
    CHECK(ring_centre(w, level).x == w.bodies[2].pos.x);
    CHECK(ring_centre(w, level).y == w.bodies[2].pos.y);
    CHECK(ring_centre(w, level).x != where.x);   // it rode the moon on instead of freezing
}

TEST_CASE("the ring stays where its carrier died") {
    Level level = empty_level();
    level.target = {100, 560};   // the ring's fallback point, well away from the carrier
    level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
    level.bodies.push_back({.pos = {500, 480}, .vel = {0, -300}, .mass = 0.5, .radius = 12, .anchor = false});
    level.target_body = 1;

    World w = launch(level, {0, 0});
    for (int i = 0; i < 240 && w.bodies.size() == 2 && w.bodies[1].alive; ++i)
        step(w, level, 1.0, step_s);
    REQUIRE(w.bodies.size() == 2);
    REQUIRE_FALSE(w.bodies[1].alive);
    const Vec died = w.bodies[1].pos;

    run(w, level, 1.0, 240);
    CHECK(ring_centre(w, level).x == died.x);
    CHECK(ring_centre(w, level).y == died.y);
}

TEST_CASE("a dead body pulls nothing and is skipped by collisions and crashes") {
    const Body anchor{.pos = {900, 300}, .mass = 1.0, .radius = 40};
    const Body dead{.pos = {500, 300}, .mass = 5.0, .radius = 40, .anchor = false, .alive = false};
    const std::vector<Body> live{anchor}, with_dead{anchor, dead};
    CHECK(acceleration(with_dead, {100, 300}, 1.0).x == acceleration(live, {100, 300}, 1.0).x);
    CHECK(acceleration(with_dead, {100, 300}, 1.0).y == acceleration(live, {100, 300}, 1.0).y);

    Level level = empty_level();
    level.target = {100, 560};   // out of the way
    level.bodies = with_dead;
    World w = launch(level, {0, 0});
    w.probe.pos = dead.pos;   // sitting where the dead body is
    CHECK(probe_outcome(w, level) == Outcome::Flying);

    Level crossing = empty_level();
    crossing.target = {100, 560};
    crossing.bodies = {{.pos = {500, 300}, .mass = 5.0, .radius = 40, .anchor = true, .alive = false},
                       {.pos = {500, 300}, .mass = 0.5, .radius = 10, .anchor = false}};
    World c = launch(crossing, {0, 0});
    step(c, crossing, 1.0, step_s);
    REQUIRE(c.bodies.size() == 2);
    CHECK(c.bodies[1].alive);   // it flew through a dead body, not into one
}

TEST_CASE("a moon stays in orbit at any gravity") {
    for (double gravity : {0.5, 1.0, 2.0}) {
        Level level{.start = {20, 20}, .target = {980, 580}, .target_radius = 10};
        level.bodies.push_back({.pos = {500, 300}, .mass = 1.0, .radius = 40});
        level.bodies.push_back(orbiting(level.bodies, 0, 120, 0.0, 0.05, 10));
        World w = launch(level, {0, 0}, gravity);
        double least = 1e9, most = 0;
        for (int i = 0; i < 60 * 240; ++i) {
            step(w, level, gravity, 1.0 / 240.0);
            const double r = length(w.bodies[1].pos - w.bodies[0].pos);
            least = std::min(least, r);
            most = std::max(most, r);
        }
        INFO("gravity " << gravity);
        CHECK(least > 120 * 0.9);
        CHECK(most < 120 * 1.1);
    }
}
