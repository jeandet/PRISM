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

// --- symbolic icons: one-color icons drawn in the color of the text beside them ---

TEST_CASE("a symbolic icon keeps its shape (alpha) and turns white, ready to be tinted") {
    auto grey = *Image::from_rgba(2, 1, {230, 230, 240, 255, 230, 230, 240, 0});
    auto s = symbolic(grey);
    REQUIRE(s);
    CHECK(s->symbolic());
    CHECK_FALSE(grey->symbolic());
    CHECK(s->id() != grey->id());   // uploaded on its own
    auto px = s->pixels();
    CHECK(std::vector<std::uint8_t>(px.begin(), px.end()) == std::vector<std::uint8_t>{255, 255, 255, 255, 255, 255, 255, 0});
}

TEST_CASE("an image command carries a tint, white by default") {
    auto img = *Image::from_rgba(1, 1, {255, 255, 255, 255});
    DrawList dl;
    dl.image(img, R(0, 0, 4, 4));
    dl.image(img, R(0, 0, 4, 4), Color::rgba(10, 20, 30));
    CHECK(std::get<ImageCmd>(dl.commands[0]).tint == Color::rgba(255, 255, 255));
    CHECK(std::get<ImageCmd>(dl.commands[1]).tint == Color::rgba(10, 20, 30));
}

TEST_CASE("SVG export bakes the tint into the embedded pixels") {
    auto img = symbolic(*Image::from_rgba(1, 1, {200, 200, 200, 255}));
    DrawList white, tinted;
    white.image(img, R(0, 0, 4, 4));
    tinted.image(img, R(0, 0, 4, 4), Color::rgba(30, 30, 35));
    CHECK(prism::render::to_svg(white) != prism::render::to_svg(tinted));
}

TEST_CASE("a button draws a symbolic icon in its text color, and a color icon as it is") {
    auto mono = symbolic(*Image::from_rgba(1, 1, {230, 230, 240, 255}));
    auto photo = *Image::from_rgba(1, 1, {200, 50, 50, 255});
    prism::core::Field<prism::ui::Button> quiet{{.text = "Reset", .icon = mono, .primary = false}};
    prism::core::Field<prism::ui::Button> main{{.text = "Save", .icon = mono}};
    prism::core::Field<prism::ui::Button> logo{{.text = "Logo", .icon = photo, .primary = false}};
    DrawList a, b, c;
    auto n1 = node_for_button(), n2 = node_for_button(), n3 = node_for_button();
    prism::ui::Widget<prism::ui::Button>::record(a, quiet, n1);
    prism::ui::Widget<prism::ui::Button>::record(b, main, n2);
    prism::ui::Widget<prism::ui::Button>::record(c, logo, n3);
    CHECK(commands_of<ImageCmd>(a).at(0).tint == commands_of<TextCmd>(a).at(0).color);
    CHECK(commands_of<ImageCmd>(b).at(0).tint == theme.text_on_primary);
    CHECK(commands_of<ImageCmd>(c).at(0).tint == Color::rgba(255, 255, 255));
}
