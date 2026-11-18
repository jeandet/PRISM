#pragma once

#include <prism/render/draw_list.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prism::ui {
using namespace prism::render;


struct TextMeasurer;
struct Clipboard;
struct UrlOpener;

// How many wire colours a theme names, by wire slot. Matches nodes::wire_slots (a test keeps the
// two equal) without ui including nodes.
inline constexpr std::size_t node_wire_slots = 6;

// `t` is the weight of `b` in [0, 1]: mix(a, b, 0) is a, mix(a, b, 1) is b. Every channel mix,
// alpha included, rounded to nearest.
inline Color mix(Color a, Color b, float t) {
    auto channel = [t](std::uint8_t from, std::uint8_t to) {
        return static_cast<std::uint8_t>(float(from) + (float(to) - float(from)) * t + 0.5f);
    };
    return Color::rgba(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a));
}

struct Theme {
    // Surface backgrounds (widget fill)
    Color surface         = Color::rgba(45, 45, 55);
    Color surface_hover   = Color::rgba(55, 55, 68);
    Color surface_active  = Color::rgba(65, 65, 78);

    // Primary accent (buttons)
    Color primary         = Color::rgba(40, 105, 180);
    Color primary_hover   = Color::rgba(50, 120, 200);
    Color primary_active  = Color::rgba(30, 90, 160);
    Color primary_outline = Color::rgba(60, 140, 220);

    // Accent (slider thumbs, checked checkboxes — purer blue)
    Color accent          = Color::rgba(0, 140, 200);
    Color accent_hover    = Color::rgba(0, 160, 220);
    Color accent_active   = Color::rgba(0, 120, 180);

    // Text
    Color text            = Color::rgba(220, 220, 220);
    Color text_muted      = Color::rgba(180, 180, 190);
    Color text_placeholder= Color::rgba(120, 120, 130);
    Color text_on_primary = Color::rgba(240, 240, 240);

    // Borders & outlines
    Color border          = Color::rgba(90, 90, 105);
    Color border_hover    = Color::rgba(120, 120, 135);
    Color focus_ring      = Color::rgba(80, 160, 240);

    // Track (slider, scrollbar track backgrounds)
    Color track           = Color::rgba(60, 60, 70);
    Color track_hover     = Color::rgba(70, 70, 82);

    // Scrollbar thumb
    Color scrollbar_thumb = Color::rgba(120, 120, 130, 160);

    // Split-pane divider
    Color divider        = Color::rgba(90, 90, 100, 160);
    Color divider_hover   = Color::rgba(130, 130, 145, 200);

    // Text cursor
    Color cursor          = Color::rgba(220, 220, 240);
    Color text_selection  = Color::rgba(60, 100, 180, 140);
    Color text_match      = Color::rgba(230, 170, 50, 90);   // highlighted ranges (search hits)

    // Documents (text/look.hpp): links, code, and the syntax colors a highlighter's spans take.
    Color link            = Color::rgba(110, 170, 255);
    Color code_bg         = Color::rgba(36, 36, 46);
    Color syntax_keyword  = Color::rgba(198, 120, 221);
    Color syntax_type     = Color::rgba(229, 192, 123);
    Color syntax_string   = Color::rgba(152, 195, 121);
    Color syntax_number   = Color::rgba(209, 154, 102);
    Color syntax_comment  = Color::rgba(140, 146, 158);
    Color syntax_preprocessor = Color::rgba(224, 108, 117);
    Color syntax_function = Color::rgba(97, 175, 239);
    Color current_line    = Color::rgba(255, 255, 255, 14);   // a code editor's caret line
    Color gutter          = Color::rgba(255, 255, 255, 9);    // its line numbers' column, over any surface

    // Dropdown popup
    Color popup_bg        = Color::rgba(50, 50, 62);
    Color popup_border    = Color::rgba(80, 80, 95);
    Color error           = Color::rgba(235, 90, 90);   // input that was refused
    Color popup_highlight = Color::rgba(60, 100, 180);
    Color scrim           = Color::rgba(0, 0, 0, 140);   // dims the window behind a modal dialog

    // Table
    Color table_row_even  = Color::rgba(30, 30, 50);
    Color table_row_odd   = Color::rgba(26, 26, 46);
    Color table_selected  = Color::rgba(50, 50, 120);
    Color table_header    = Color::rgba(42, 42, 74);
    Color table_divider   = Color::rgba(50, 50, 70);
    Color table_header_divider = Color::rgba(74, 74, 106);
    Color table_header_text    = Color::rgba(136, 136, 204);

    // Tabs
    Color tab_bar_bg      = Color::rgba(42, 42, 58);
    Color tab_active_bg   = Color::rgba(30, 30, 46);
    Color tab_text        = Color::rgba(140, 140, 160);
    Color tab_text_active = Color::rgba(220, 220, 240);
    Color tab_accent      = Color::rgba(124, 111, 255);

    // Window chrome
    Color chrome_bg       = Color::rgba(45, 45, 48);
    Color chrome_border   = Color::rgba(60, 60, 65);
    Color chrome_text     = Color::rgba(200, 200, 200);
    Color chrome_icon     = Color::rgba(180, 180, 180);
    Color chrome_close    = Color::rgba(196, 43, 28);

    // Canvas clear color
    Color canvas_bg       = Color::rgba(30, 30, 30);

    // The node board (a graph of cards on a grid, src/app/node_board.cpp). A family is a kind of
    // node ("source", "math"): its colour tints the card's header and names it on the right.
    struct NodeFamily {
        std::string name;
        Color color;
        bool operator==(const NodeFamily&) const = default;
    };
    Color node_board_bg   = Color::rgba(14, 16, 22);
    Color node_grid       = Color::rgba(38, 42, 54);
    bool  node_grid_dots  = true;    // dots (the dark look) or lines (the light one)
    Color node_surface    = Color::rgba(25, 28, 38);
    Color node_border     = Color::rgba(42, 47, 61);
    Color node_border_selected = Color::rgba(58, 66, 88);
    Color node_glow       = Color::rgba(0, 140, 200, 89);   // accent at 35 %
    // By wire slot: waveform, number, boolean, integer, text, then the neutral one every app type shares.
    std::array<Color, node_wire_slots> node_wires{
        Color::rgba(78, 161, 255), Color::rgba(242, 163, 58), Color::rgba(95, 211, 141),
        Color::rgba(169, 139, 255), Color::rgba(238, 241, 247), Color::rgba(154, 161, 178)};
    Color node_probe_bg   = Color::rgba(31, 36, 51);
    Color node_probe_text = Color::rgba(207, 227, 255);
    std::vector<NodeFamily> node_families{   // the built-in ones; an app registers its own (set_family_color)
        {"source", Color::rgba(63, 208, 201)},
        {"filter", Color::rgba(169, 139, 255)},
        {"math", Color::rgba(242, 163, 58)},
        {"display", Color::rgba(95, 211, 141)}};
    Color node_family_default = Color::rgba(154, 161, 178);   // a family not in the table
    bool  dark = true;   // the dark look or the light one: picks an app family's colour (set_family_color)

    // How text is measured; set by the widget tree from the backend. Null = monospace estimate.
    // Points at a backend-owned object: do not copy a Theme holding it beyond the backend's
    // lifetime. Set before the logic thread starts recording; read only on the logic thread.
    const TextMeasurer* measurer = nullptr;
    // Where copy/cut puts text; same ownership rules as measurer. Null = copy does nothing.
    const Clipboard* clipboard = nullptr;
    const UrlOpener* url_opener = nullptr;   // links open through it; null: they only count clicks
};

inline Theme default_theme() { return {}; }

inline Theme dark_theme() { return {}; }

// Dark text on light surfaces; every text color keeps at least WCAG AA contrast (4.5:1) with
// what it is drawn on (tests/test_theme.cpp checks it).
inline Theme light_theme() {
    Theme t;
    auto c = [](std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) { return Color::rgba(r, g, b, a); };
    t.surface = c(236, 237, 242);         t.surface_hover = c(226, 228, 235);   t.surface_active = c(214, 217, 226);
    t.link = c(0, 88, 190);               t.code_bg = c(224, 226, 233);
    t.syntax_keyword = c(150, 30, 150);   t.syntax_type = c(140, 90, 0);       t.syntax_string = c(40, 110, 40);
    t.syntax_number = c(160, 70, 0);      t.syntax_comment = c(95, 100, 110);  t.syntax_preprocessor = c(190, 20, 60);
    t.syntax_function = c(0, 90, 190);       t.current_line = c(0, 0, 0, 12);       t.gutter = c(0, 0, 0, 9);
    t.primary = c(30, 100, 190);          t.primary_hover = c(40, 112, 205);    t.primary_active = c(22, 86, 168);
    t.primary_outline = c(60, 130, 220);
    t.accent = c(47, 111, 222);           t.accent_hover = c(64, 128, 235);     t.accent_active = c(36, 92, 196);
    t.text = c(30, 30, 35);               t.text_muted = c(85, 85, 95);
    t.text_placeholder = c(140, 140, 150); t.text_on_primary = c(255, 255, 255);
    t.border = c(190, 192, 200);          t.border_hover = c(160, 163, 173);    t.focus_ring = c(40, 120, 220);
    t.track = c(215, 217, 224);           t.track_hover = c(200, 203, 212);
    t.scrollbar_thumb = c(120, 120, 130, 140);
    t.divider = c(180, 182, 190, 180);    t.divider_hover = c(140, 143, 155, 220);
    t.cursor = c(30, 30, 40);             t.text_selection = c(60, 120, 220, 90); t.text_match = c(240, 180, 40, 110);
    t.popup_bg = c(252, 252, 254);        t.popup_border = c(190, 192, 200);
    t.error = c(200, 40, 40);
    t.popup_highlight = c(30, 100, 190);  t.scrim = c(0, 0, 0, 90);
    t.table_row_even = c(255, 255, 255);  t.table_row_odd = c(246, 247, 250);   t.table_selected = c(205, 222, 250);
    t.table_header = c(232, 234, 240);    t.table_divider = c(225, 227, 233);
    t.table_header_divider = c(205, 208, 216); t.table_header_text = c(70, 80, 140);
    t.tab_bar_bg = c(232, 234, 240);      t.tab_active_bg = c(250, 250, 252);
    t.tab_text = c(100, 100, 115);        t.tab_text_active = c(25, 25, 35);    t.tab_accent = c(90, 80, 230);
    t.chrome_bg = c(232, 233, 238);       t.chrome_border = c(205, 207, 214);
    t.chrome_text = c(40, 40, 48);        t.chrome_icon = c(70, 70, 80);        t.chrome_close = c(196, 43, 28);
    t.canvas_bg = c(250, 250, 252);
    t.node_board_bg = c(245, 244, 240);   t.node_grid = c(235, 233, 227);      t.node_grid_dots = false;
    t.node_surface = c(255, 255, 255);    t.node_border = c(20, 22, 30, 31);
    t.node_border_selected = c(70, 76, 96); t.node_glow = c(47, 111, 222, 89);
    t.node_wires = {c(47, 111, 222), c(217, 130, 43), c(31, 122, 69),
                    c(122, 90, 248), c(36, 38, 43), c(94, 99, 110)};
    t.node_probe_bg = c(255, 255, 255);   t.node_probe_text = c(36, 38, 43);
    t.node_families = {{"source", c(26, 158, 143)},
                       {"filter", c(122, 90, 248)},
                       {"math", c(217, 130, 43)},
                       {"display", c(31, 122, 69)}};
    t.node_family_default = c(120, 126, 138);
    t.dark = false;
    return t;
}

// An app's own family, in the colour it has on the dark look and on the light one. Registered by
// name, once, whatever the theme: a theme switch keeps it. A family named here wins over the
// theme's table. Safe to call from any thread.
void set_family_color(std::string name, Color dark, Color light);

// A family's colour: the registered ones first, then the theme's table, then the colour given to
// families neither names.
Color node_family_color(const Theme& t, std::string_view family);

} // namespace prism::ui
