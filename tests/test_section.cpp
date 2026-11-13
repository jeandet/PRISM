// A section the user folds and unfolds (vb.section with a Field<Expander>): its content exists
// only while it is open, so nothing hidden takes the focus or a click.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <algorithm>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

namespace {
struct Settings {
    Field<Expander> advanced{{.title = "Advanced"}};
    Field<Checkbox> verbose{{.checked = false, .label = "Verbose logs"}};
    Field<Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.section(advanced, [&](WidgetTree::ViewBuilder& inner) { inner.widget(verbose); });
            vb.widget(ok);
        });
    }
};

struct Fixture {
    Settings m;
    WidgetTree tree{m};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(300, 300, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(300, 300, ++v); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    void click_header() {
        route_event(tree, snap.get(), MouseButton{Point{X{20}, Y{10}}, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{Point{X{20}, Y{10}}, buttons::left, false});
        frame();
    }
};
}

TEST_CASE("a closed section shows its title only; a click opens it") {
    Fixture f;
    CHECK(f.shows("Advanced"));
    CHECK_FALSE(f.shows("Verbose logs"));
    f.click_header();
    CHECK(f.m.advanced.get().open);
    CHECK(f.shows("Verbose logs"));
    f.click_header();
    CHECK_FALSE(f.shows("Verbose logs"));
}

TEST_CASE("what is folded away takes no focus") {
    Fixture f;
    auto before = f.tree.leaf_ids().size();
    f.m.advanced.set({.title = "Advanced", .open = true});   // the app opens it too
    f.frame();
    CHECK(f.tree.leaf_ids().size() == before + 1);
    f.m.advanced.set({.title = "Advanced", .open = false});
    f.frame();
    for (int i = 0; i < 4; ++i) {
        f.tree.focus_next();
        CHECK(f.tree.focused_id() != 0);
    }
    CHECK(f.tree.leaf_ids().size() == before);
}

TEST_CASE("its header says whether it is open") {
    Field<Expander> e{{.title = "More", .open = true}};
    AccessInfo a;
    Widget<Expander>::describe(e, a);
    CHECK(a.name == "More");
    CHECK(a.expanded == std::optional<bool>{true});
}
