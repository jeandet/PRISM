// A Field<Color> shows a swatch and its hex code; a click opens a popup with a
// saturation/brightness square and a hue bar.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/event_routing.hpp>
#include <prism/app/widget_tree.hpp>

#include <string>
#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::input;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;
using prism::app::widget_detail::route_event;

TEST_CASE("hex codes: written in upper case, read with or without #") {
    CHECK(color_detail::to_hex(Color::rgba(0x33, 0x66, 0xCC)) == "#3366CC");
    CHECK(color_detail::parse_hex("#3366cc") == Color::rgba(0x33, 0x66, 0xCC));
    CHECK(color_detail::parse_hex("3366CC") == Color::rgba(0x33, 0x66, 0xCC));
    CHECK_FALSE(color_detail::parse_hex("#33z6CC"));
    CHECK_FALSE(color_detail::parse_hex("#3366C"));
}

TEST_CASE("HSV and back gives the same color") {
    for (Color c : {Color::rgba(255, 0, 0), Color::rgba(0x33, 0x66, 0xCC), Color::rgba(128, 128, 128),
                    Color::rgba(0, 0, 0), Color::rgba(255, 255, 255), Color::rgba(12, 200, 99)}) {
        auto hsv = color_detail::to_hsv(c);
        CHECK(color_detail::from_hsv(hsv, c.a) == c);
    }
    auto red = color_detail::to_hsv(Color::rgba(255, 0, 0));
    CHECK(red.h == doctest::Approx(0));
    CHECK(red.s == doctest::Approx(1));
    CHECK(red.v == doctest::Approx(1));
    CHECK(color_detail::to_hsv(Color::rgba(0, 0, 255)).h == doctest::Approx(240));
}

namespace {
struct Paint {
    Field<Color> ink{Color::rgba(0x33, 0x66, 0xCC, 200)};
    void view(WidgetTree::ViewBuilder& vb) { vb.widget(ink); }
};

struct Fixture {
    Paint p;
    WidgetTree tree{p};
    std::unique_ptr<SceneSnapshot> snap = tree.build_snapshot(400, 400, 1);
    uint64_t v = 1;
    void frame() { snap = tree.build_snapshot(400, 400, ++v); }
    void press(Point at) { route_event(tree, snap.get(), MouseButton{at, buttons::left, true}); }
    void release(Point at) { route_event(tree, snap.get(), MouseButton{at, buttons::left, false}); frame(); }
    void click(Point at) { press(at); release(at); }
    void key(int32_t k) { route_event(tree, snap.get(), KeyPress{k, 0}); frame(); }
    bool shows(std::string_view text) const {
        for (auto& dl : snap->draw_lists)
            for (auto& c : dl->commands)
                if (auto* t = std::get_if<TextCmd>(&c); t && t->text == text) return true;
        return false;
    }
    std::vector<Rect> popup_images() const {   // the square, then the hue bar
        std::vector<Rect> out;
        for (auto& c : snap->overlay.commands)
            if (auto* i = std::get_if<ImageCmd>(&c)) out.push_back(i->rect);
        return out;
    }
    void open() {
        click(Point{X{10}, Y{10}});
        REQUIRE(popup_images().size() == 2);
    }
};

Point inside(Rect r, float fx, float fy) {
    return Point{r.origin.x + DX{r.extent.w.raw() * fx}, r.origin.y + DY{r.extent.h.raw() * fy}};
}
}

TEST_CASE("closed, it shows its hex code and no popup") {
    Fixture f;
    CHECK(f.shows("#3366CC"));
    CHECK(f.popup_images().empty());
}

TEST_CASE("a click in the square sets saturation and brightness; the hue and the alpha stay") {
    Fixture f;
    f.open();
    auto square = f.popup_images()[0];
    f.click(inside(square, 0.999f, 0.001f));   // top right: full saturation, full brightness
    auto hsv = color_detail::to_hsv(f.p.ink.get());
    CHECK(hsv.h == doctest::Approx(220).epsilon(0.02));
    CHECK(hsv.s == doctest::Approx(1).epsilon(0.02));
    CHECK(hsv.v == doctest::Approx(1).epsilon(0.02));
    CHECK(f.p.ink.get().a == 200);
    CHECK(f.popup_images().size() == 2);   // still open: the user may keep adjusting

    f.click(inside(square, 0.5f, 0.999f));   // bottom: black
    CHECK(f.p.ink.get() == Color::rgba(0, 0, 0, 200));
}

TEST_CASE("a drag in the hue bar changes the hue, even through black, which has none") {
    Fixture f;
    f.open();
    auto square = f.popup_images()[0];
    auto bar = f.popup_images()[1];
    f.click(inside(square, 0.5f, 0.999f));   // black: no hue of its own
    f.press(inside(bar, 0.5f, 0.5f));
    route_event(f.tree, f.snap.get(), MouseMove{inside(bar, 0.5f, 1.f / 3.f)});
    f.release(inside(bar, 0.5f, 1.f / 3.f));
    f.click(inside(square, 0.999f, 0.001f));   // back to full brightness: the hue held
    CHECK(color_detail::to_hsv(f.p.ink.get()).h == doctest::Approx(120).epsilon(0.03));
}

TEST_CASE("arrows nudge the color in the open popup; Escape closes it; a click outside too") {
    Fixture f;
    f.open();
    auto before = color_detail::to_hsv(f.p.ink.get());
    f.key(keys::down);
    CHECK(color_detail::to_hsv(f.p.ink.get()).v < before.v);
    f.key(keys::escape);
    CHECK(f.popup_images().empty());

    f.open();
    f.click(Point{X{390}, Y{390}});
    CHECK(f.popup_images().empty());
}

TEST_CASE("the keyboard opens it: Space or Enter on the focused swatch") {
    Fixture f;
    f.key(keys::tab);
    f.key(keys::space);
    CHECK(f.popup_images().size() == 2);
}

TEST_CASE("dragging the hue of black redraws the square, though the color stays black") {
    Fixture f;
    f.open();
    auto square = f.popup_images()[0];
    auto bar = f.popup_images()[1];
    f.click(inside(square, 0.5f, 0.999f));   // black
    auto image_of_square = [&] {
        for (auto& c : f.snap->overlay.commands)
            if (auto* i = std::get_if<ImageCmd>(&c)) return i->image;
        return std::shared_ptr<const Image>{};
    };
    f.press(inside(bar, 0.5f, 0.5f));
    route_event(f.tree, f.snap.get(), MouseMove{inside(bar, 0.5f, 0.4f)});   // the first move also hovers
    f.frame();
    auto before = image_of_square();
    route_event(f.tree, f.snap.get(), MouseMove{inside(bar, 0.5f, 0.25f)});   // the drag goes on
    f.frame();
    CHECK(f.p.ink.get() == Color::rgba(0, 0, 0, 200));
    CHECK(image_of_square() != before);
}

TEST_CASE("Tab away closes the popup") {
    Fixture f;
    f.open();
    f.key(keys::tab);
    CHECK(f.popup_images().empty());
}

TEST_CASE("near the window's right edge the popup slides left to stay inside") {
    struct Right {
        Field<Color> ink{Color::rgba(0x33, 0x66, 0xCC)};
        void view(WidgetTree::ViewBuilder& vb) { vb.hstack([&] { vb.spacer(); vb.widget(ink); }); }
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
        if (auto* i = std::get_if<ImageCmd>(&c)) right = std::max(right, i->rect.origin.x.raw() + i->rect.extent.w.raw());
    CHECK(right > 0.f);
    CHECK(right <= 400.f);
}
