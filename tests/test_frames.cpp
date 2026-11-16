// Containers with a look of their own: vb.toolbar, vb.status_bar, vb.group (ui/frame.hpp).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

#include <string_view>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace {
struct Window {
    Field<Button> bold{{.text = "Bold", .primary = false, .checked = false}};
    Field<Button> save{{.text = "Save", .primary = false}};
    Field<TextArea<>> body{{}};
    Field<Checkbox> wrap{{.checked = true, .label = "Wrap"}};
    Field<Label<>> status{{"Ready"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.toolbar([&] { vb.widget(bold); vb.widget(save); });
            vb.widget(body);
            vb.group("Options", [&] { vb.widget(wrap); });
            vb.spacer();
            vb.status_bar([&] { vb.widget(status); });
        });
    }
};

constexpr float win_w = 500, win_h = 400;

Rect rect_of(const SceneSnapshot& s, WidgetId id) {
    for (auto& [gid, r] : s.geometry)
        if (gid == id) return r;
    FAIL("no geometry for widget");
    return {};
}

bool has_fill(const SceneSnapshot& s, Color c, Rect where) {
    for (auto& dl : s.draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* r = std::get_if<FilledRect>(&cmd); r && r->color == c && r->rect == where) return true;
    return false;
}

bool draws_text(const SceneSnapshot& s, std::string_view text) {
    for (auto& dl : s.draw_lists)
        for (auto& cmd : dl->commands)
            if (auto* t = std::get_if<TextCmd>(&cmd); t && t->text == text) return true;
    return false;
}
}

TEST_CASE("a toolbar is a filled strip across the window, its buttons inset in it") {
    Window w;
    WidgetTree tree{w};
    auto snap = tree.build_snapshot(win_w, win_h, 1);
    auto ids = tree.leaf_ids();   // bold, save, body, wrap, status
    Rect bold = rect_of(*snap, ids[0]);
    CHECK(bold.origin.x.raw() > 0.f);
    CHECK(bold.origin.y.raw() > 0.f);
    float strip_h = bold.origin.y.raw() * 2 + bold.extent.h.raw();
    CHECK(has_fill(*snap, tree.theme().chrome_bg, Rect{Point{X{0}, Y{0}}, Size{Width{win_w}, Height{strip_h}}}));
    CHECK(rect_of(*snap, ids[2]).origin.y.raw() >= strip_h);   // the text area starts below it
}

TEST_CASE("a status bar sits at the bottom of the window") {
    Window w;
    WidgetTree tree{w};
    auto snap = tree.build_snapshot(win_w, win_h, 1);
    Rect status = rect_of(*snap, tree.leaf_ids()[4]);
    float gap = win_h - (status.origin.y.raw() + status.extent.h.raw());
    CHECK(gap > 0.f);
    CHECK(gap < 8.f);
}

TEST_CASE("a group shows its title above its content") {
    Window w;
    WidgetTree tree{w};
    auto snap = tree.build_snapshot(win_w, win_h, 1);
    CHECK(draws_text(*snap, "Options"));
    Rect wrap = rect_of(*snap, tree.leaf_ids()[3]);
    Rect body = rect_of(*snap, tree.leaf_ids()[2]);
    CHECK(wrap.origin.y.raw() - (body.origin.y.raw() + body.extent.h.raw()) >= 20.f);   // room for the title
    CHECK(wrap.origin.x.raw() > 0.f);
}

TEST_CASE("frames take the colours of the theme in use") {
    Window w;
    WidgetTree tree{w};
    (void)tree.build_snapshot(win_w, win_h, 1);
    tree.set_theme(light_theme());
    auto snap = tree.build_snapshot(win_w, win_h, 2);
    Rect bold = rect_of(*snap, tree.leaf_ids()[0]);
    float strip_h = bold.origin.y.raw() * 2 + bold.extent.h.raw();
    CHECK(has_fill(*snap, light_theme().chrome_bg, Rect{Point{X{0}, Y{0}}, Size{Width{win_w}, Height{strip_h}}}));
}

namespace {
// A status bar's content as a block of its own: left text, a spacer, right text.
struct Status {
    Field<Label<>> left{{"Ready"}};
    Field<Label<>> right{{"Ln 1"}};
    void view(WidgetTree::ViewBuilder& vb) {
        vb.widget(left);
        vb.spacer();
        vb.widget(right);
    }
};
struct WithStatusBlock {
    Field<TextArea<>> body{{}};
    Status status;
    void view(WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(body);
            vb.status_bar([&] { vb.block(status); });
        });
    }
};
}

TEST_CASE("a block in a status bar lays out as the bar's row: its spacer pushes the right text right") {
    WithStatusBlock w;
    WidgetTree tree{w};
    auto snap = tree.build_snapshot(win_w, win_h, 1);
    auto ids = tree.leaf_ids();   // body, left, right
    Rect left = rect_of(*snap, ids[1]);
    Rect right = rect_of(*snap, ids[2]);
    CHECK(left.origin.y.raw() == doctest::Approx(right.origin.y.raw()));   // one row
    CHECK(left.origin.x.raw() <= 8.f);
    float right_gap = win_w - (right.origin.x.raw() + right.extent.w.raw());
    CHECK(right_gap >= 0.f);
    CHECK(right_gap <= 8.f);
}
