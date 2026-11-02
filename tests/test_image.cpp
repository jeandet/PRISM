#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/render/draw_list.hpp>
#include <prism/render/svg_export.hpp>

#include <vector>

using namespace prism::render;
using namespace prism::core;

namespace {
Point P(float x, float y) { return {X{x}, Y{y}}; }
Rect R(float x, float y, float w, float h) { return {P(x, y), Size{Width{w}, Height{h}}}; }
}

TEST_CASE("an image holds RGBA pixels and rejects a size mismatch") {
    auto ok = Image::from_rgba(2, 1, std::vector<std::uint8_t>(8, 255));
    REQUIRE(ok);
    CHECK((*ok)->width() == 2);
    CHECK((*ok)->height() == 1);
    CHECK_FALSE(Image::from_rgba(2, 2, std::vector<std::uint8_t>(8, 255)));   // needs 16 bytes
    CHECK_FALSE(Image::from_rgba(0, 1, {}));
}

TEST_CASE("every image has its own id, so backends can cache uploads by it") {
    auto a = *Image::from_rgba(1, 1, {0, 0, 0, 255});
    auto b = *Image::from_rgba(1, 1, {0, 0, 0, 255});
    CHECK(a->id() != b->id());
}

TEST_CASE("an image command is translated like any other and counts in the bounding box") {
    auto img = *Image::from_rgba(1, 1, {255, 0, 0, 255});
    DrawList dl;
    dl.clip_push(P(10, 20), Size{Width{100}, Height{100}});
    dl.image(img, R(5, 5, 16, 16));
    dl.clip_pop();
    auto* cmd = std::get_if<ImageCmd>(&dl.commands[1]);
    REQUIRE(cmd);
    CHECK(cmd->rect.origin.x.raw() == doctest::Approx(15.f));
    CHECK(cmd->rect.origin.y.raw() == doctest::Approx(25.f));
    CHECK(cmd->image == img);
}

TEST_CASE("SVG export draws an image as a placeholder of the right size") {
    auto img = *Image::from_rgba(1, 1, {255, 0, 0, 255});
    DrawList dl;
    dl.image(img, R(5, 6, 16, 17));
    auto svg = prism::render::to_svg(dl);
    CHECK(svg.find("x=\"5\" y=\"6\" width=\"16\" height=\"17\"") != std::string::npos);
}

// --- a button with an icon ---

#include <prism/ui/delegate.hpp>
#include <prism/ui/widget_node.hpp>

namespace {
prism::ui::Theme theme;
prism::ui::WidgetNode node_for_button() {
    prism::ui::WidgetNode n;
    n.theme = &theme;
    return n;
}
template <typename Cmd>
std::vector<Cmd> commands_of(const DrawList& dl) {
    std::vector<Cmd> out;
    for (auto& c : dl.commands) if (auto* x = std::get_if<Cmd>(&c)) out.push_back(*x);
    return out;
}
}

TEST_CASE("a button's icon is drawn left of its text, and pushes the text right") {
    auto icon = *Image::from_rgba(1, 1, {255, 255, 255, 255});
    prism::core::Field<prism::ui::Button> plain{{"Save"}};
    prism::core::Field<prism::ui::Button> with_icon{{.text = "Save", .icon = icon}};
    DrawList a, b;
    auto n1 = node_for_button(), n2 = node_for_button();
    prism::ui::Widget<prism::ui::Button>::record(a, plain, n1);
    prism::ui::Widget<prism::ui::Button>::record(b, with_icon, n2);
    auto images = commands_of<ImageCmd>(b);
    REQUIRE(images.size() == 1);
    CHECK(images[0].image == icon);
    CHECK(images[0].rect.extent.w.raw() == doctest::Approx(16.f));
    auto text_plain = commands_of<TextCmd>(a), text_icon = commands_of<TextCmd>(b);
    REQUIRE(text_plain.size() == 1);
    REQUIRE(text_icon.size() == 1);
    CHECK(text_icon[0].origin.x.raw() > images[0].rect.origin.x.raw() + 16.f);
    CHECK(text_icon[0].origin.x.raw() > text_plain[0].origin.x.raw());
    CHECK(prism::ui::Widget<prism::ui::Button>::intrinsic_width(with_icon, n2)
          > prism::ui::Widget<prism::ui::Button>::intrinsic_width(plain, n1));
}

TEST_CASE("an icon-only button is square-ish and draws no text") {
    auto icon = *Image::from_rgba(1, 1, {255, 255, 255, 255});
    prism::core::Field<prism::ui::Button> b{{.text = "", .icon = icon}};
    DrawList dl;
    auto n = node_for_button();
    prism::ui::Widget<prism::ui::Button>::record(dl, b, n);
    CHECK(commands_of<TextCmd>(dl).empty());
    CHECK(prism::ui::Widget<prism::ui::Button>::intrinsic_width(b, n) == doctest::Approx(32.f));
}
