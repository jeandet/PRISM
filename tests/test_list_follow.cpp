// vb.list(items).follow_tail(): a log or console list stays on its newest row while rows are
// appended, stops when the user scrolls up, and follows again once back at the bottom.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <string>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Console {
    List<std::string> lines;
    void view(WidgetTree::ViewBuilder& vb) { vb.list(lines).follow_tail(); }
};

struct Fixture {
    Console m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap;
    uint64_t v = 0;
    Fixture() { frame(); frame(); }
    void frame() {
        snap = tree.build_snapshot(300, 200, ++v);
        tree.clear_dirty();
    }
    void append(int from, int to) {
        for (int i = from; i < to; ++i) m.lines.push_back("line " + std::to_string(i));
        frame();
        frame();
    }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    void scroll(float dy) {
        route_event(tree, snap.get(), MouseScroll{Point{X{100}, Y{100}}, DX{0}, DY{dy}});
        frame();
        frame();
    }
};
}

TEST_CASE("appended rows scroll a following list to its newest row") {
    Fixture f;
    f.append(0, 100);
    CHECK(f.shows("line 99"));
    CHECK_FALSE(f.shows("line 0"));
    f.append(100, 150);
    CHECK(f.shows("line 149"));
}

TEST_CASE("scrolled up, the list stays put; back at the bottom, it follows again") {
    Fixture f;
    f.append(0, 100);
    f.scroll(30.f);   // up, away from the newest rows
    REQUIRE_FALSE(f.shows("line 99"));
    f.append(100, 110);
    CHECK_FALSE(f.shows("line 109"));   // the user is reading: leave the view alone
    f.scroll(-1000.f);                  // all the way down
    f.append(110, 120);
    CHECK(f.shows("line 119"));
}

TEST_CASE("a list that does not follow stays at the top") {
    struct Plain {
        List<std::string> lines;
        void view(WidgetTree::ViewBuilder& vb) { vb.list(lines); }
    } m;
    WidgetTree tree{m};
    for (int i = 0; i < 100; ++i) m.lines.push_back("line " + std::to_string(i));
    std::unique_ptr<SceneSnapshot> snap;
    for (uint64_t v = 1; v < 4; ++v) snap = tree.build_snapshot(300, 200, v);
    bool first = false;
    for (auto& dl : snap->draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&c); t && t->text == "line 0") first = true;
    CHECK(first);
}
