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
