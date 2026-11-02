#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
std::string many_lines(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) s += "line " + std::to_string(i) + "\n";
    return s;
}

struct Editor {
    Field<TextArea<>> body{{.value = many_lines(60), .rows = 10}};
    Field<TextArea<>> note{{.value = "short", .rows = 3}};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(body, note); }
};

struct Fixture {
    Editor model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 600, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 600, ++v); }
    WidgetId body_id() const { return tree.leaf_ids()[0]; }
    WidgetId note_id() const { return tree.leaf_ids()[1]; }
    Rect rect_of(WidgetId id) const { return *find_widget_rect(*snap, id); }
    Point centre(WidgetId id) const {
        auto r = rect_of(id);
        return Point{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
    }
    // Whether the body draws `text` in the current frame (lines above the view are skipped).
    bool body_draws(std::string_view text) const {
        for (std::size_t i = 0; i < snap->geometry.size(); ++i) {
            if (snap->geometry[i].first != body_id() || !snap->draw_lists[i]) continue;
            for (auto& c : snap->draw_lists[i]->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        }
        return false;
    }
    std::optional<Rect> thumb_of(WidgetId id) const {
        for (auto& [oid, r] : snap->overlay_geometry) if (oid == id) return r;   // [0] is the thumb
        return std::nullopt;
    }
};
}

TEST_CASE("the wheel scrolls a text area that holds more lines than it shows") {
    Fixture f;
    REQUIRE(f.body_draws("line 0"));
    route_event(f.tree, f.snap.get(), MouseScroll{f.centre(f.body_id()), DX{0}, DY{1}});
    f.frame();
    CHECK_FALSE(f.body_draws("line 0"));
    route_event(f.tree, f.snap.get(), MouseScroll{f.centre(f.body_id()), DX{0}, DY{-100}});
    f.frame();
    CHECK(f.body_draws("line 0"));   // clamped at the top
}

TEST_CASE("a text area with hidden lines shows a scrollbar; one that fits does not") {
    Fixture f;
    auto thumb = f.thumb_of(f.body_id());
    REQUIRE(thumb);
    auto body = f.rect_of(f.body_id());
    CHECK(thumb->origin.x.raw() > body.origin.x.raw() + body.extent.w.raw() / 2);   // on the right
    CHECK(thumb->extent.h.raw() < body.extent.h.raw());
    CHECK_FALSE(f.thumb_of(f.note_id()));
}

TEST_CASE("dragging the scrollbar thumb scrolls the text area") {
    Fixture f;
    auto thumb = f.thumb_of(f.body_id());
    REQUIRE(thumb);
    Point grab{thumb->origin.x + DX{2}, thumb->origin.y + DY{5}};
    route_event(f.tree, f.snap.get(), MouseButton{grab, buttons::left, true});
    route_event(f.tree, f.snap.get(), MouseMove{Point{grab.x, grab.y + DY{60}}, buttons::left_mask});
    route_event(f.tree, f.snap.get(), MouseButton{Point{grab.x, grab.y + DY{60}}, buttons::left, false});
    f.frame();
    CHECK_FALSE(f.body_draws("line 0"));
    auto moved = f.thumb_of(f.body_id());
    REQUIRE(moved);
    CHECK(moved->origin.y.raw() > thumb->origin.y.raw());
}

TEST_CASE("moving the mouse over a scrolled text area does not snap it back to the caret") {
    Fixture f;
    route_event(f.tree, f.snap.get(), MouseScroll{f.centre(f.body_id()), DX{0}, DY{3}});
    f.frame();
    REQUIRE_FALSE(f.body_draws("line 0"));
    route_event(f.tree, f.snap.get(), MouseMove{f.centre(f.body_id())});
    route_event(f.tree, f.snap.get(), MouseMove{Point{f.centre(f.body_id()).x + DX{3}, f.centre(f.body_id()).y}});
    f.frame();
    CHECK_FALSE(f.body_draws("line 0"));   // still scrolled; the caret (line 0) stays off-view
}

TEST_CASE("moving the caret brings it back into view") {
    Fixture f;
    route_event(f.tree, f.snap.get(), MouseButton{f.centre(f.body_id()), buttons::left, true});   // caret mid-view
    route_event(f.tree, f.snap.get(), MouseButton{f.centre(f.body_id()), buttons::left, false});
    f.frame();
    auto caret_line_shown = [&] { return f.body_draws("line 4") || f.body_draws("line 5"); };
    REQUIRE(caret_line_shown());
    route_event(f.tree, f.snap.get(), MouseScroll{f.centre(f.body_id()), DX{0}, DY{10}});
    f.frame();
    REQUIRE_FALSE(caret_line_shown());
    route_event(f.tree, f.snap.get(), KeyPress{keys::left, 0});
    f.frame();
    CHECK(caret_line_shown());
}
