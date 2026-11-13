// An image shown on its own (ui::Picture): at its own size, or fitted into a box.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/app/widget_tree.hpp>

#include <vector>

using namespace prism;
using namespace prism::core;
using namespace prism::render;
using namespace prism::ui;
using prism::app::WidgetTree;

namespace {
std::shared_ptr<const Image> image(int w, int h) {
    return *Image::from_rgba(w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w * h * 4), 200));
}

struct Page {
    Field<Picture> logo;
    Field<Button> ok{{"OK"}};
    void view(WidgetTree::ViewBuilder& vb) { vb.vstack(logo, ok); }
};

std::optional<Rect> image_rect(const SceneSnapshot& s) {
    for (auto& dl : s.draw_lists)
        for (auto& c : dl->commands)
            if (auto* i = std::get_if<ImageCmd>(&c)) return i->rect;
    return std::nullopt;
}

Rect rect_of(const SceneSnapshot& s, WidgetId id) {
    for (auto& [gid, r] : s.geometry)
        if (gid == id) return r;
    FAIL("no geometry for widget");
    return {};
}
}

TEST_CASE("a picture is drawn at the image's own size") {
    Page p{.logo = {{.image = image(40, 20)}}};
    WidgetTree tree{p};
    auto snap = tree.build_snapshot(300, 200, 1);
    auto r = image_rect(*snap);
    REQUIRE(r);
    CHECK(r->extent == Size{Width{40}, Height{20}});
    CHECK(rect_of(*snap, tree.leaf_ids()[1]).origin.y.raw() >= 20.f);   // the button below it
}

TEST_CASE("in a box, it is scaled to fit with its proportions kept, and centred") {
    Page p{.logo = {{.image = image(40, 20), .size = Size{Width{100}, Height{100}}}}};
    WidgetTree tree{p};
    auto snap = tree.build_snapshot(300, 300, 1);
    auto r = image_rect(*snap);
    REQUIRE(r);
    CHECK(r->extent == Size{Width{100}, Height{50}});
    CHECK(r->origin.y.raw() == doctest::Approx(25.f));
    CHECK(rect_of(*snap, tree.leaf_ids()[1]).origin.y.raw() >= 100.f);   // the whole box is the widget's
}

TEST_CASE("with no image yet it keeps its box, and assistive tech hears its description") {
    Page p{.logo = {{.size = Size{Width{64}, Height{64}}, .description = "Company logo"}}};
    WidgetTree tree{p};
    auto snap = tree.build_snapshot(300, 300, 1);
    CHECK_FALSE(image_rect(*snap));
    CHECK(rect_of(*snap, tree.leaf_ids()[1]).origin.y.raw() >= 64.f);
    AccessInfo a;
    Widget<Picture>::describe(p.logo, a);
    CHECK(a.name == "Company logo");
}
