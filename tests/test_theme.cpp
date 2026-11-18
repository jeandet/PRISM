#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <prism/ui/context.hpp>
#include <prism/core/field.hpp>
#include <prism/app/widget_tree.hpp>
namespace prism::core {} namespace prism::render {} namespace prism::input {}
namespace prism::ui {} namespace prism::app {} namespace prism::plot {}
namespace prism {
using namespace core; using namespace render; using namespace input;
using namespace ui; using namespace app; using namespace plot;
}


TEST_CASE("default_theme returns consistent values") {
    auto t = prism::default_theme();
    CHECK(t.surface.r == 45);
    CHECK(t.surface.g == 45);
    CHECK(t.surface.b == 55);
    CHECK(t.primary.r == 40);
    CHECK(t.primary.g == 105);
    CHECK(t.primary.b == 180);
    CHECK(t.focus_ring.r == 80);
    CHECK(t.focus_ring.g == 160);
    CHECK(t.focus_ring.b == 240);
    CHECK(t.accent.r == 0);
    CHECK(t.accent.g == 140);
    CHECK(t.accent.b == 200);
}

TEST_CASE("Theme is copy-constructible and modifiable") {
    auto t = prism::default_theme();
    t.primary = prism::Color::rgba(255, 0, 0);
    CHECK(t.primary.r == 255);
    auto t2 = prism::default_theme();
    CHECK(t2.primary.r == 40);
}

struct ThemeTestModel {
    prism::Field<bool> toggle{false};
    prism::Field<std::string> name{"hello"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(toggle, name);
    }
};

TEST_CASE("WidgetTree propagates theme to all nodes") {
    ThemeTestModel model;
    prism::WidgetTree tree(model);
    auto& root = tree.root();
    CHECK(root.theme != nullptr);
    for (auto& child : root.children) {
        CHECK(child.theme != nullptr);
        CHECK(child.theme == root.theme);
    }
}

TEST_CASE("WidgetTree theme matches default_theme") {
    ThemeTestModel model;
    prism::WidgetTree tree(model);
    CHECK(tree.theme().primary.r == 40);
    CHECK(tree.theme().primary.g == 105);
}

TEST_CASE("Theme has divider colors distinct from scrollbar thumb") {
    prism::Theme t;
    bool has_color = t.divider.r != 0 || t.divider.g != 0 || t.divider.b != 0;
    CHECK(has_color);
    CHECK(t.divider_hover.a > t.divider.a);
}

namespace {
// WCAG 2 contrast ratio (https://www.w3.org/TR/WCAG21/#dfn-contrast-ratio): 4.5 is AA for text.
double luminance(prism::Color c) {
    auto lin = [](double v) { v /= 255.0; return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}
double contrast(prism::Color a, prism::Color b) {
    double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
void check_readable(const prism::Theme& t) {
    CHECK(contrast(t.text, t.surface) >= 4.5);
    CHECK(contrast(t.text, t.canvas_bg) >= 4.5);
    CHECK(contrast(t.text, t.popup_bg) >= 4.5);
    CHECK(contrast(t.text_on_primary, t.primary) >= 4.5);
    CHECK(contrast(t.text_on_primary, t.popup_highlight) >= 4.5);
    CHECK(contrast(t.tab_text_active, t.tab_active_bg) >= 4.5);
    CHECK(contrast(t.chrome_text, t.chrome_bg) >= 4.5);
    CHECK(contrast(t.text, t.table_row_even) >= 4.5);
    CHECK(contrast(t.text, t.table_selected) >= 4.5);
    CHECK(contrast(t.text_muted, t.surface) >= 3.0);
}

struct Labelled {
    prism::Field<prism::Label<>> hello{{"hello"}};
    void view(prism::WidgetTree::ViewBuilder& vb) { vb.widget(hello); }
};

std::optional<prism::Color> text_color(const prism::SceneSnapshot& snap, std::string_view text) {
    for (auto& dl : snap.draw_lists)
        for (auto& c : dl->commands)
            if (auto* t = std::get_if<prism::TextCmd>(&c); t && t->text == text) return t->color;
    return std::nullopt;
}
}

TEST_CASE("the dark theme is the default; both themes keep text readable") {
    CHECK(prism::dark_theme().surface == prism::default_theme().surface);
    check_readable(prism::dark_theme());
    auto light = prism::light_theme();
    check_readable(light);
    CHECK(luminance(light.canvas_bg) > luminance(light.text));   // dark text on a light window
}

TEST_CASE("set_theme repaints every widget, and keeps the backend's measurer") {
    Labelled model;
    prism::WidgetTree tree(model);
    struct Fixed final : prism::TextMeasurer {
        float width(std::string_view t, float, prism::render::FontStyle) const override { return 5.f * float(t.size()); }
    } measurer;
    tree.set_text_measurer(&measurer);
    auto snap = tree.build_snapshot(300, 200, 1);
    CHECK(text_color(*snap, "hello") == prism::dark_theme().text_muted);   // a label is muted text
    tree.set_theme(prism::light_theme());
    snap = tree.build_snapshot(300, 200, 2);
    CHECK(text_color(*snap, "hello") == prism::light_theme().text_muted);
    CHECK(tree.theme().measurer == &measurer);
}

TEST_CASE("the snapshot carries the window's own colors, for the backend") {
    Labelled model;
    prism::WidgetTree tree(model);
    auto snap = tree.build_snapshot(300, 200, 1);
    CHECK(snap->window.background == prism::dark_theme().canvas_bg);
    tree.set_theme(prism::light_theme());
    snap = tree.build_snapshot(300, 200, 2);
    CHECK(snap->window.background == prism::light_theme().canvas_bg);
    CHECK(snap->window.chrome_bg == prism::light_theme().chrome_bg);
    CHECK(snap->window.chrome_text == prism::light_theme().chrome_text);
}

TEST_CASE("mix weights the second colour's every channel") {
    using prism::Color;
    const Color surface = Color::rgba(25, 28, 38);      // a dark card
    const Color source = Color::rgba(63, 208, 201);     // its family
    CHECK(prism::ui::mix(surface, source, 0.f) == surface);
    CHECK(prism::ui::mix(surface, source, 1.f) == source);
    CHECK(prism::ui::mix(surface, source, 0.2f) == Color::rgba(33, 64, 71));      // the header
    CHECK(prism::ui::mix(Color::rgba(220, 220, 220), source, 0.6f) == Color::rgba(126, 213, 209));
    CHECK(prism::ui::mix(Color::rgba(10, 20, 30, 0), Color::rgba(10, 20, 30, 255), 0.5f).a == 128);
}

namespace {
// The node board's look as the plan's Global Constraints name it: look A is the dark theme, look B
// the light one. The mockups' header tints do not fall out of the mixing rule the plan states, so
// the rule wins: a header is mix(surface, family, 0.2), its family label mix(text, family, 0.6).
struct BoardLook {
    prism::Color board_bg, grid, surface, border, border_selected, probe_bg, probe_text, family_default;
    bool dots;
    std::array<prism::Color, prism::ui::node_wire_slots> wires;   // by wire slot
    std::vector<prism::Theme::NodeFamily> families;
};

prism::Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    return prism::Color::rgba(r, g, b, a);
}

void check_look(const prism::Theme& t, const BoardLook& want) {
    CHECK(t.node_board_bg == want.board_bg);
    CHECK(t.node_grid == want.grid);
    CHECK(t.node_grid_dots == want.dots);
    CHECK(t.node_surface == want.surface);
    CHECK(t.node_border == want.border);
    CHECK(t.node_border_selected == want.border_selected);
    CHECK(t.node_probe_bg == want.probe_bg);
    CHECK(t.node_probe_text == want.probe_text);
    CHECK(t.node_family_default == want.family_default);
    CHECK(t.node_wires == want.wires);
    CHECK(t.node_families == want.families);
    CHECK(t.node_glow == rgba(t.accent.r, t.accent.g, t.accent.b, 89));   // the accent at 35 %
}

const BoardLook look_a{
    .board_bg = rgba(14, 16, 22),        // #0e1016
    .grid = rgba(38, 42, 54),            // #262a36
    .surface = rgba(25, 28, 38),         // #191c26
    .border = rgba(42, 47, 61),          // #2a2f3d
    .border_selected = rgba(58, 66, 88), // #3a4258
    .probe_bg = rgba(31, 36, 51),        // #1f2433
    .probe_text = rgba(207, 227, 255),   // #cfe3ff
    .family_default = rgba(154, 161, 178),
    .dots = true,
    .wires = {rgba(78, 161, 255), rgba(242, 163, 58), rgba(95, 211, 141), rgba(169, 139, 255),
              rgba(238, 241, 247), rgba(154, 161, 178)},
    .families = {{"source", rgba(63, 208, 201)}, {"filter", rgba(169, 139, 255)},
                 {"math", rgba(242, 163, 58)}, {"display", rgba(95, 211, 141)}},
};

const BoardLook look_b{
    .board_bg = rgba(245, 244, 240),        // #f5f4f0
    .grid = rgba(235, 233, 227),            // #ebe9e3
    .surface = rgba(255, 255, 255),         // #ffffff
    .border = rgba(20, 22, 30, 31),         // rgba(20,22,30,0.12)
    .border_selected = rgba(70, 76, 96),
    .probe_bg = rgba(255, 255, 255),
    .probe_text = rgba(36, 38, 43),         // #24262b
    .family_default = rgba(120, 126, 138),
    .dots = false,
    .wires = {rgba(47, 111, 222), rgba(217, 130, 43), rgba(31, 122, 69), rgba(122, 90, 248),
              rgba(36, 38, 43), rgba(94, 99, 110)},
    .families = {{"source", rgba(26, 158, 143)}, {"filter", rgba(122, 90, 248)},
                 {"math", rgba(217, 130, 43)}, {"display", rgba(31, 122, 69)}},
};
}

TEST_CASE("the dark theme is the board's look A") { check_look(prism::dark_theme(), look_a); }

TEST_CASE("the light theme is the board's look B") {
    check_look(prism::light_theme(), look_b);
    CHECK(prism::light_theme().accent == rgba(47, 111, 222));   // #2f6fde, with the waveform wire
}
