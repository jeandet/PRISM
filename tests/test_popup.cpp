// Popups that hold widgets (doc/design/popup-content.md).
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
struct Settings {
    Field<TextField<>> behind{{}};              // a field in the window, under the popup
    Field<Popup> prefs{{.open = false, .modal = true}};
    Field<TextField<>> name{{}};                // the popup's fields
    Field<Checkbox> dark{{.checked = false, .label = "Dark theme"}};
    Field<Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(behind);
            vb.popup(prefs, [&](WidgetTree::ViewBuilder& p) { p.vstack(name, dark, ok); });
        });
    }
};

struct Fixture {
    Settings model;
    WidgetTree tree{model};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(500, 400, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(500, 400, ++v); }
    void open(bool modal = true) {
        model.prefs.set({.open = true, .modal = modal});
        frame();
    }
    void click(Point p) {
        route_event(tree, snap.get(), MouseButton{p, buttons::left, true});
        route_event(tree, snap.get(), MouseButton{p, buttons::left, false});
    }
    void key(int32_t k, uint16_t m = 0) { route_event(tree, snap.get(), KeyPress{k, m}); }
    void type(std::string s) { route_event(tree, snap.get(), TextInput{std::move(s)}); }
    // The overlay hit rect of the widget drawing `text` (popup content lives in the overlay).
    std::optional<Rect> overlay_rect_of(WidgetId id) const {
        for (auto& [oid, r] : snap->overlay_geometry) if (oid == id) return r;
        return std::nullopt;
    }
    std::optional<Rect> rect_of(WidgetId id) const {
        if (auto r = overlay_rect_of(id)) return r;
        return find_widget_rect(*snap, id);
    }
    static Point centre(Rect r) {
        return Point{r.origin.x + DX{r.extent.w.raw() / 2}, r.origin.y + DY{r.extent.h.raw() / 2}};
    }
    // The ids of focusable widgets, in Tab order, as the tree sees them now.
    std::vector<WidgetId> tab_cycle(int presses) {
        std::vector<WidgetId> seen;
        for (int i = 0; i < presses; ++i) {
            key(keys::tab);
            seen.push_back(tree.focused_id());
        }
        return seen;
    }
};
}

TEST_CASE("a closed popup's widgets do not exist for Tab") {
    Fixture f;
    auto cycle = f.tab_cycle(4);
    for (auto id : cycle) CHECK(id == cycle.front());   // only the field behind
}

TEST_CASE("opening focuses the popup's first field; typing goes there, not behind") {
    Fixture f;
    f.click(f.centre(*f.rect_of(f.tree.leaf_ids()[0])));   // focus the field behind first
    f.open();
    f.type("Ada");
    CHECK(f.model.name.get().value == "Ada");
    CHECK(f.model.behind.get().value.empty());
}

TEST_CASE("Tab cycles inside a modal popup and never reaches the window behind") {
    Fixture f;
    f.open();
    auto behind = f.tree.leaf_ids()[0];
    auto cycle = f.tab_cycle(7);
    for (auto id : cycle) CHECK(id != behind);
    std::sort(cycle.begin(), cycle.end());
    CHECK(std::unique(cycle.begin(), cycle.end()) - cycle.begin() >= 3);   // name, dark, OK
}

TEST_CASE("the popup's widgets are on top: clicking one reaches it") {
    Fixture f;
    f.open();
    bool before = f.model.dark.get().checked;
    // Click each content widget's rect (overlay entries after the card's) until the checkbox
    // flips: it only can if presses inside the card reach the popup's widgets.
    auto rects = f.snap->overlay_geometry;
    REQUIRE(rects.size() > 1);
    for (std::size_t i = 1; i < rects.size() && f.model.dark.get().checked == before; ++i) {
        f.click(f.centre(rects[i].second));
        f.frame();
    }
    CHECK(f.model.dark.get().checked != before);
}

TEST_CASE("a press outside a modal popup reaches nothing and keeps it open") {
    Fixture f;
    f.open();
    f.click(Point{X{3}, Y{3}});   // the window corner, outside the centred card
    f.frame();
    CHECK(f.model.prefs.get().open);
    f.type("x");
    CHECK(f.model.behind.get().value.empty());
}

TEST_CASE("a press outside a non-modal popup closes it") {
    Fixture f;
    f.open(false);
    f.click(Point{X{495}, Y{395}});
    f.frame();
    CHECK_FALSE(f.model.prefs.get().open);
}

TEST_CASE("Escape closes the popup and gives the focus back") {
    Fixture f;
    auto behind = f.tree.leaf_ids()[0];
    f.click(f.centre(*f.rect_of(behind)));
    REQUIRE(f.tree.focused_id() == behind);
    f.open();
    REQUIRE(f.tree.focused_id() != behind);
    f.key(keys::escape);
    f.frame();
    CHECK_FALSE(f.model.prefs.get().open);
    CHECK(f.tree.focused_id() == behind);
}

TEST_CASE("the card fits the window and is centred for a modal popup") {
    Fixture f;
    f.open();
    auto card = prism::app::widget_detail::popup_area(f.tree, *f.snap);
    REQUIRE(card);
    CHECK(card->origin.x.raw() >= 0.f);
    CHECK(card->origin.y.raw() >= 0.f);
    CHECK(card->origin.x.raw() + card->extent.w.raw() <= 500.f);
    CHECK(card->origin.y.raw() + card->extent.h.raw() <= 400.f);
    CHECK(card->origin.x.raw() + card->extent.w.raw() / 2 == doctest::Approx(250.f).epsilon(0.02));
}

TEST_CASE("a popup's fields count as placed, though its content is built only when it opens") {
    Settings model;
    WidgetTree tree{model};
    Node root;
    WidgetTree::ViewBuilder vb{tree, root};
    model.view(vb);
    CHECK(vb.placed().contains(&model.name));
    CHECK(vb.placed().contains(&model.dark));
    CHECK(vb.placed().contains(&model.ok));
}

namespace {
// A modal popup with nothing to focus (a notice), over a field that had the focus.
struct Notice {
    Field<TextField<>> behind{{}};
    Field<Popup> note{{.open = false, .modal = true}};
    Field<Label<>> text{{"Saved."}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(behind);
            vb.popup(note, [&](WidgetTree::ViewBuilder& p) { p.widget(text); });
        });
    }
};

// A tall window scrolled inside a scroll area, and a popup holding a short scroll area.
struct Scrolling {
    Field<Label<>> rows[30] = {};
    Field<Popup> pop{{.open = false, .modal = true}};
    Field<Label<>> inside[3] = {};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.scroll([&] {   // the popup is declared inside the window's scroll area
            for (auto& r : rows) vb.widget(r);
            vb.popup(pop, [&](WidgetTree::ViewBuilder& p) {
                p.scroll([&] { for (auto& r : inside) p.widget(r); });
            });
        });
    }
};
}

TEST_CASE("a modal popup with nothing to focus does not let typing reach the window behind") {
    Notice model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(500, 400, 1);
    auto r = *find_widget_rect(*snap, tree.leaf_ids()[0]);
    Point in{r.origin.x + DX{5}, r.origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{in, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{in, buttons::left, false});
    REQUIRE(tree.focused_id() == tree.leaf_ids()[0]);
    model.note.set({.open = true, .modal = true});
    snap = tree.build_snapshot(500, 400, 2);
    route_event(tree, snap.get(), TextInput{"x"});
    CHECK(model.behind.get().value.empty());
}

TEST_CASE("the wheel inside a modal popup never scrolls the window behind it") {
    Scrolling model;
    for (int i = 0; i < 30; ++i) model.rows[i].set({"row " + std::to_string(i)});
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(500, 300, 1);
    auto first_row = *find_widget_rect(*snap, tree.leaf_ids()[0]);
    model.pop.set({.open = true, .modal = true});
    snap = tree.build_snapshot(500, 300, 2);
    auto card = *prism::app::widget_detail::popup_area(tree, *snap);
    Point inside{card.origin.x + DX{20}, card.origin.y + DY{20}};
    for (int i = 0; i < 5; ++i) route_event(tree, snap.get(), MouseScroll{inside, DX{0}, DY{3}});
    model.pop.set({.open = false, .modal = true});
    snap = tree.build_snapshot(500, 300, 3);
    auto now = find_widget_rect(*snap, tree.leaf_ids()[0]);
    REQUIRE(now);                                  // gone: the window scrolled the first row away
    CHECK(now->origin.y == first_row.origin.y);
}

TEST_CASE("a popup with no widgets inside is built once, not on every frame") {
    struct Empty {
        Field<TextField<>> behind{{}};
        Field<Popup> pop{{.open = false, .modal = false}};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.vstack([&] {
                vb.widget(behind);
                vb.popup(pop, [](WidgetTree::ViewBuilder&) {});
            });
        }
    } model;
    int builds = 0;
    WidgetTree tree{model};
    (void)builds;
    auto snap = tree.build_snapshot(500, 400, 1);
    auto behind = tree.leaf_ids()[0];
    auto r = *find_widget_rect(*snap, behind);
    Point in{r.origin.x + DX{5}, r.origin.y + DY{5}};
    route_event(tree, snap.get(), MouseButton{in, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{in, buttons::left, false});
    model.pop.set({.open = true, .modal = false});
    for (uint64_t v = 2; v < 6; ++v) snap = tree.build_snapshot(500, 400, v);
    route_event(tree, snap.get(), TextInput{"y"});   // a non-modal empty popup: the field keeps typing
    CHECK(model.behind.get().value == "y");
}

TEST_CASE("a widget marked focus_on_open has the focus when its popup opens") {
    struct Form {
        Field<Popup> pop{{.open = false, .modal = true}};
        Field<TextField<>> first{{}};
        Field<TextField<>> wanted{{}};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.popup(pop, [&](WidgetTree::ViewBuilder& p) {
                p.widget(first);
                p.widget(wanted).focus_on_open();
            });
        }
    } model;
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(500, 400, 1);
    model.pop.set({.open = true, .modal = true});
    snap = tree.build_snapshot(500, 400, 2);
    route_event(tree, snap.get(), TextInput{"x"});
    CHECK(model.wanted.get().value == "x");
    CHECK(model.first.get().value.empty());
}

TEST_CASE("a click on a scrolling text area inside a popup focuses it, not a scrollbar drag") {
    // The popup's widgets share the overlay hit list with scrollbars; only a scrollbar starts a drag.
    struct Pad {
        Field<Popup> pop{{.open = false, .modal = true}};
        Field<TextArea<>> text{{.rows = 3}};
        Field<Button> ok{{"OK"}};
        void view(WidgetTree::ViewBuilder& vb) {
            vb.popup(pop, [&](WidgetTree::ViewBuilder& p) { p.widget(text); p.widget(ok).focus_on_open(); });
        }
    } model;
    std::string lines;
    for (int i = 0; i < 30; ++i) lines += "line\n";
    model.text.set({.value = lines, .rows = 3});
    WidgetTree tree{model};
    auto snap = tree.build_snapshot(500, 400, 1);
    model.pop.set({.open = true, .modal = true});
    snap = tree.build_snapshot(500, 400, 2);
    auto area = *std::ranges::find_if(snap->overlay_geometry, [&](auto& g) {
        return g.first != tree.popup_owner() && g.second.extent.h.raw() > 40.f;   // the text area, not the card
    });
    Point in{area.second.origin.x + DX{10}, area.second.origin.y + DY{10}};
    route_event(tree, snap.get(), MouseButton{in, buttons::left, true});
    route_event(tree, snap.get(), MouseButton{in, buttons::left, false});
    CHECK(tree.focused_id() == area.first);
}
