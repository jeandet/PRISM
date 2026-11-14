// A Field<std::chrono::year_month_day> shows the date; a click opens a month calendar.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;
using namespace std::chrono;

TEST_CASE("ISO text: written and read back; bad dates are refused") {
    CHECK(date_detail::to_iso(2026y / November / 14d) == "2026-11-14");
    CHECK(date_detail::parse_iso("2026-02-28") == year_month_day{2026y / February / 28d});
    CHECK_FALSE(date_detail::parse_iso("2026-02-30"));
    CHECK_FALSE(date_detail::parse_iso("2026-2-3"));
    CHECK_FALSE(date_detail::parse_iso("tomorrow"));
}

TEST_CASE("a month's grid starts on the Monday on or before its first day") {
    CHECK(date_detail::grid_start(2026y / November) == year_month_day{2026y / October / 26d});   // Nov 1 is a Sunday
    CHECK(date_detail::grid_start(2026y / June) == year_month_day{2026y / June / 1d});           // June 1 is a Monday
}

TEST_CASE("a month later keeps the day, or the month's last one") {
    CHECK(date_detail::add_months(2026y / January / 31d, 1) == year_month_day{2026y / February / 28d});
    CHECK(date_detail::add_months(2026y / March / 15d, -3) == year_month_day{2025y / December / 15d});
}

namespace {
struct Trip {
    Field<year_month_day> leave{2026y / November / 14d};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(leave); }
};

struct Fixture {
    Trip m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 400, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 400, ++v); }
    void click(Point at) {
        route_event(tree, snap.get(), MouseButton{at, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{at, buttons::left, false});
        frame();
    }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    // Where the open calendar draws `text` (a day number, a month title, an arrow).
    std::optional<Point> popup_text(std::string_view text) const {
        for (auto& c : snap->overlay.commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return t->origin + Offset{DX{3}, DY{6}};
        return std::nullopt;
    }
    bool open() const { return popup_text("November 2026") || popup_text("December 2026") || popup_text("October 2026"); }
};
}

TEST_CASE("closed, it shows the date; a click opens the month") {
    Fixture f;
    CHECK(f.shows("2026-11-14"));
    CHECK_FALSE(f.open());
    f.click(Point{X{10}, Y{10}});
    CHECK(f.popup_text("November 2026"));
    CHECK(f.popup_text("Mo"));
}

TEST_CASE("a click on a day picks it and closes the calendar") {
    Fixture f;
    f.click(Point{X{10}, Y{10}});
    auto day = f.popup_text("20");
    REQUIRE(day);
    f.click(*day);
    CHECK(f.m.leave.get() == year_month_day{2026y / November / 20d});
    CHECK_FALSE(f.open());
    CHECK(f.shows("2026-11-20"));
}

TEST_CASE("the arrows in the header page through months without picking") {
    Fixture f;
    f.click(Point{X{10}, Y{10}});
    f.click(*f.popup_text("\xe2\x80\xba"));   // ›
    CHECK(f.popup_text("December 2026"));
    CHECK(f.m.leave.get() == year_month_day{2026y / November / 14d});
    auto day = f.popup_text("3");
    REQUIRE(day);
    f.click(*day);
    CHECK(f.m.leave.get() == year_month_day{2026y / December / 3d});
}

TEST_CASE("keys in the open calendar: arrows by day and week, pages by month, Escape closes") {
    Fixture f;
    f.key(keys::tab);
    f.key(keys::enter);
    CHECK(f.open());
    f.key(keys::right);
    CHECK(f.m.leave.get() == year_month_day{2026y / November / 15d});
    f.key(keys::down);
    CHECK(f.m.leave.get() == year_month_day{2026y / November / 22d});
    f.key(keys::page_up);
    CHECK(f.m.leave.get() == year_month_day{2026y / October / 22d});
    CHECK(f.popup_text("October 2026"));
    f.key(keys::escape);
    CHECK_FALSE(f.open());
}

TEST_CASE("near the window's right edge the calendar slides left to stay inside") {
    struct Right {
        Field<year_month_day> leave{2026y / November / 14d};
        void view(WidgetTree::ViewBuilder& vb) { vb.hstack([&] { vb.spacer(); vb.widget(leave); }); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 400, 1);
    Rect box{};
    for (auto& [id, r] : snap->geometry)
        if (r.extent.w.raw() > 0 && r.extent.w.raw() < 200) box = r;
    REQUIRE(box.origin.x.raw() > 250.f);
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, true});
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, false});
    snap = tree.build_snapshot(400, 400, 2);
    float right = 0;
    for (auto& c : snap->overlay.commands)
        if (auto* r = std::get_if<FilledRect>(&c)) right = std::max(right, r->rect.origin.x.raw() + r->rect.extent.w.raw());
    CHECK(right > 0.f);
    CHECK(right <= 400.f);
}

TEST_CASE("an open calendar leaves the app's shortcuts working") {
    struct Form {
        Field<year_month_day> leave{2026y / November / 14d};
        Field<Button> save{{.text = "Save", .shortcut = {keys::s, mods::ctrl}}};
        void view(WidgetTree::ViewBuilder& vb) { vb.vstack(leave, save); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 400, 1);
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::left, false});
    snap = tree.build_snapshot(400, 400, 2);
    REQUIRE_FALSE(snap->overlay.commands.empty());
    route_event(tree, snap.get(), KeyPress{keys::s, mods::ctrl});
    CHECK(m.save.get().click_count == 1);
}

TEST_CASE("a date that is not one (a default-made field) opens on a real month") {
    struct Blank {
        Field<year_month_day> when{};
        void view(WidgetTree::ViewBuilder& vb) { vb.widget(when); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 400, 1);
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{Point{X{10}, Y{10}}, buttons::left, false});
    snap = tree.build_snapshot(400, 400, 2);
    bool titled = false;
    for (auto& c : snap->overlay.commands)
        if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "January 1970") titled = true;
    CHECK(titled);
}

namespace {
// The rectangle of the widget drawing `text`, as on screen.
Rect rect_showing(const SceneSnapshot& snap, std::string_view text) {
    for (std::size_t i = 0; i < snap.draw_lists.size(); ++i)
        for (auto& c : snap.draw_lists[i]->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return snap.geometry[i].second;
    return {};
}
Rect popup_card(const SceneSnapshot& snap) {
    for (auto& c : snap.overlay.commands)
        if (auto* r = std::get_if<FilledRect>(&c)) return r->rect;   // the card's background comes first
    return {};
}
}

TEST_CASE("in a scrolled view the calendar opens at the box, not where the box would be unscrolled") {
    struct Long {
        std::array<Field<int>, 30> filler{};
        Field<year_month_day> leave{2026y / November / 14d};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.scroll([&] {
                for (auto& f : filler) vb.widget(f);
                vb.widget(leave);
            });
        }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 400, 1);
    for (int i = 0; i < 40; ++i)
        route_event(tree, snap.get(), MouseScroll{Point{X{100}, Y{200}}, DX{0}, DY{-3}});   // towards the user: down
    snap = tree.build_snapshot(400, 400, 2);
    Rect box = rect_showing(*snap, "2026-11-14");
    REQUIRE(box.extent.h.raw() > 0);
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, true});
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, false});
    snap = tree.build_snapshot(400, 400, 3);
    Rect card = popup_card(*snap);
    float box_top = box.origin.y.raw(), box_bottom = box_top + box.extent.h.raw();
    float card_top = card.origin.y.raw(), card_bottom = card_top + card.extent.h.raw();
    CHECK((std::abs(card_top - box_bottom) < 2.f || std::abs(card_bottom - box_top) < 2.f));
}

TEST_CASE("an open calendar follows its box when the window narrows") {
    struct Narrowing {
        Field<year_month_day> leave{2026y / November / 14d};
        void view(WidgetTree::ViewBuilder& vb) { vb.hstack([&] { vb.spacer(); vb.widget(leave); }); }
    } m;
    WidgetTree tree{m};
    auto snap = tree.build_snapshot(400, 400, 1);
    Rect box = rect_showing(*snap, "2026-11-14");
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, true});
    route_event(tree, snap.get(), MouseButton{box.center(), buttons::left, false});
    snap = tree.build_snapshot(400, 400, 2);
    snap = tree.build_snapshot(300, 400, 3);
    Rect card = popup_card(*snap);
    CHECK(card.extent.w.raw() > 0);
    CHECK(card.origin.x.raw() + card.extent.w.raw() <= 300.f);
}
