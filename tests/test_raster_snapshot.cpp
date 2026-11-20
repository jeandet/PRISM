#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/raster/canvas.hpp>
#include <prism/raster/draw_snapshot.hpp>
#include <prism/raster/fonts.hpp>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <variant>

using namespace prism::core;
using prism::app::ViewBuilder;
using prism::app::WidgetTree;
using prism::render::SceneSnapshot;
using prism::render::TextCmd;

namespace {

constexpr int W = 480, H = 272;

// A small model with a hand-written view() (no reflection needed): a label, a button, a
// checkbox and a slider, laid out as the board's firmware builds them.
struct DemoModel {
    Field<std::string> title{"PRISM raster demo"};
    Field<int> counter{42};
    Field<prism::ui::Label<>> heading{{.value = "counter demo", .heading = true}};
    Field<prism::ui::Button> run{{"Run"}};
    Field<bool> enabled{true};
    Field<prism::ui::Slider<>> volume{{.value = 0.5}};

    void view(ViewBuilder& vb) {
        vb.vstack(title, counter, heading, run, enabled, volume);
    }
};

} // namespace

TEST_CASE("a laid-out snapshot draws into a 480x272 canvas") {
    DemoModel model;
    WidgetTree tree(model);
    prism::raster::Fonts fonts;   // the measurer: layout and drawing use the same metrics
    tree.set_text_measurer(&fonts);
    auto snap = tree.build_snapshot(W, H, 1);
    REQUIRE(snap != nullptr);
    tree.clear_dirty();

    std::array<std::uint32_t, W * H> fb{};
    prism::raster::Canvas canvas{fb, W, H, W};
    canvas.clear(snap->window.background);
    prism::raster::draw(canvas, *snap, fonts);

    // not all one colour
    const std::set<std::uint32_t> colors(fb.begin(), fb.end());
    CHECK(colors.size() > 1);
    // the primary button's fill is on screen: theme primary = rgba(40, 105, 180)
    CHECK(colors.contains(0xFF2869B4u));

    // The heading label's text pixels are inside the label's geometry rect. A heading draws no
    // box behind its text, so inside its text box every bright pixel is text ink.
    std::size_t list = snap->draw_lists.size();
    const TextCmd* heading = nullptr;
    for (std::size_t i = 0; i < snap->draw_lists.size(); ++i) {
        if (!snap->draw_lists[i]) continue;
        for (const auto& cmd : snap->draw_lists[i]->commands)
            if (const auto* t = std::get_if<TextCmd>(&cmd); t && t->text == "counter demo") {
                list = i;
                heading = t;
            }
    }
    REQUIRE(heading != nullptr);
    REQUIRE(list < snap->geometry.size());
    const Rect g = snap->geometry[list].second;

    const float tw = fonts.width(heading->text, heading->size, heading->font);
    const float th = fonts.line_height(heading->size, heading->font);
    const float bx0 = heading->origin.x.raw(), by0 = heading->origin.y.raw();
    const float bx1 = bx0 + tw, by1 = by0 + th;
    // the text box is laid out inside the widget's rect
    CHECK(g.origin.x.raw() <= bx0 + 0.5f);
    CHECK(g.origin.y.raw() <= by0 + 0.5f);
    CHECK(bx1 <= g.origin.x.raw() + g.extent.w.raw() + 1.f);
    CHECK(by1 <= g.origin.y.raw() + g.extent.h.raw() + 1.f);

    int ink = 0;
    // scan the text box itself (pixel centres inside [bx0, bx1) x [by0, by1))
    const int x0 = std::max(0, static_cast<int>(std::ceil(bx0 - 0.5f)));
    const int x1 = std::min(W - 1, static_cast<int>(std::ceil(bx1 - 0.5f)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::ceil(by0 - 0.5f)));
    const int y1 = std::min(H - 1, static_cast<int>(std::ceil(by1 - 0.5f)) - 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const std::uint32_t p = fb[static_cast<std::size_t>(y) * W + x];
            const int r = (p >> 16) & 0xFF, gg = (p >> 8) & 0xFF, b = p & 0xFF;
            if (r > 150 && gg > 150 && b > 150) {   // heading text is theme.text (220, 220, 220)
                ++ink;
                INFO("ink pixel at ", x, ",", y);
                CHECK(g.contains(Point{X{static_cast<float>(x) + 0.5f},
                                        Y{static_cast<float>(y) + 0.5f}}));
            }
        }
    CHECK(ink > 0);   // the text actually drew
}
