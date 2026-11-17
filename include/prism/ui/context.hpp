#pragma once

#include <prism/render/draw_list.hpp>

#include <cstdint>

namespace prism::ui {
using namespace prism::render;


struct TextMeasurer;
struct Clipboard;
struct UrlOpener;

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
    t.accent = c(0, 120, 190);            t.accent_hover = c(0, 135, 210);      t.accent_active = c(0, 105, 170);
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
    return t;
}

} // namespace prism::ui
