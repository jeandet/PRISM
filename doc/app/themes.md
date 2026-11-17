# Themes

A theme is the set of colors every widget draws with. PRISM ships a dark theme and a light one.
An app can follow the system's choice, pick one, or bring its own.

## Follow the system

One line makes the app light or dark as the system is set, and switch when the user changes it:

```cpp
#include <prism/prism.hpp>
using namespace prism;

struct Form {
    core::Field<ui::TextField<>> name{{.value = "ada", .placeholder = "Name"}};
    core::Field<ui::Slider<>> volume{{.value = 60, .min = 0, .max = 100, .step = 1,
                                      .label = "Volume", .unit = "%"}};
    core::Field<ui::Checkbox> notify{{.checked = true, .label = "Notify me"}};
    core::Field<ui::Button> save{{.text = "Save"}};

    void view(app::ViewBuilder& vb) { vb.vstack(name, volume, notify, save); }
};

int main() {
    Form form;
    return app::run(form, {.title = "Form"}, [](app::AppContext& ctx) {
        ctx.follow_system_theme();
    });
}
```

The same form, dark and light:

<img src="../screenshots/theme_dark.svg" alt="The form in the dark theme: light text on dark surfaces" width="320"/>
<img src="../screenshots/theme_light.svg" alt="The form in the light theme: dark text on light surfaces" width="320"/>

## Pick one

`ctx.set_theme(theme)` sets every window's theme at once and stops following the system:

```cpp
app::run(form, {.title = "Form"}, [](app::AppContext& ctx) {
    ctx.set_theme(ui::light_theme());
});
```

You may call it again at any time on the logic thread, from a menu choice for example. Every
widget repaints with the new colors in the same frame.

| Call | What it does |
|---|---|
| `ui::dark_theme()` | The default theme: light text on dark surfaces. |
| `ui::light_theme()` | Dark text on light surfaces. |
| `ctx.set_theme(const ui::Theme&)` | Every window takes this theme. Stops following the system. |
| `ctx.follow_system_theme()` | Light or dark as the system is set, now and when it changes. Dark until the system answers. |
| `ctx.system_prefers_dark()` | What the system asks for, last time it said. |
| `ctx.theme()` | The theme the windows use now. |

Both shipped themes keep every text at WCAG AA contrast (4.5:1) against what it is drawn on. A
test checks it.

## Make your own

A `ui::Theme` is a plain struct of colors. Start from a shipped one and change what you need:

```cpp
ui::Theme brand() {
    ui::Theme t = ui::light_theme();
    t.primary        = core::Color::rgba(200, 60, 20);   // buttons
    t.primary_hover  = core::Color::rgba(215, 75, 35);
    t.primary_active = core::Color::rgba(180, 50, 15);
    t.accent         = core::Color::rgba(200, 60, 20);   // slider thumbs, ticked boxes
    return t;
}

// in setup, or later on the logic thread:
ctx.set_theme(brand());
```

### Color roles

Each member is one role. A widget picks the role, never a color, so a new theme reaches every
widget. `_hover` and `_active` variants are used under the pointer and while pressed.

| Group | Members | Used for |
|---|---|---|
| Surfaces | `surface`, `surface_hover`, `surface_active` | The fill of input widgets: fields, boxes, combo boxes. |
| Primary | `primary`, `primary_hover`, `primary_active`, `primary_outline` | Buttons. |
| Accent | `accent`, `accent_hover`, `accent_active` | Slider thumbs, ticked checkboxes, chosen radio buttons. |
| Text | `text`, `text_muted`, `text_placeholder`, `text_on_primary` | Text; secondary text; a field's hint while empty; text on a button. |
| Borders | `border`, `border_hover`, `focus_ring` | Outlines; the ring around the widget with the keyboard focus. |
| Tracks | `track`, `track_hover`, `scrollbar_thumb` | Slider and scroll bar tracks; the scroll bar's handle. |
| Dividers | `divider`, `divider_hover` | The handle between split panes. |
| Editing | `cursor`, `text_selection`, `text_match` | The caret; selected text; search hits. |
| Documents | `link`, `code_bg`, `current_line`, `gutter` | Links; code blocks; a code editor's caret line and line-number column. |
| Syntax | `syntax_keyword`, `syntax_type`, `syntax_string`, `syntax_number`, `syntax_comment`, `syntax_preprocessor`, `syntax_function` | The colors of highlighted code. |
| Popups | `popup_bg`, `popup_border`, `popup_highlight`, `scrim` | Menus and drop-down lists; the row under the pointer; the shade behind a modal dialog. |
| Errors | `error` | Input that was refused. |
| Tables | `table_row_even`, `table_row_odd`, `table_selected`, `table_header`, `table_divider`, `table_header_divider`, `table_header_text` | Table rows, the selection, the header. |
| Tabs | `tab_bar_bg`, `tab_active_bg`, `tab_text`, `tab_text_active`, `tab_accent` | The tab bar, the open tab, its underline. |
| Window frame | `chrome_bg`, `chrome_border`, `chrome_text`, `chrome_icon`, `chrome_close` | The title bar PRISM draws, and its close button under the pointer. |
| Canvas | `canvas_bg` | The background behind everything. |

`core::Color::rgba(r, g, b, a)` makes a color; `a` defaults to 255 (opaque).

## Icons that follow the theme

A one-color icon drawn in a fixed color disappears on one of the two themes. Load it as
*symbolic*: it is then drawn in the color of the text beside it.

```cpp
std::shared_ptr<const render::Image> save_icon() {
    auto img = render::load_image("icons/save.svg", core::Size{core::Width{16}, core::Height{16}});
    return img ? render::symbolic(*img) : nullptr;   // a missing file: no icon
}

core::Field<ui::Button> save{{.text = "Save", .icon = save_icon()}};
```

A logo or a photo keeps its own colors: load it without `symbolic`.

## In Python

```python
prism.set_theme("light")    # or "dark", or "system"
```

It may be called from any thread while an app runs.

## Pitfalls

- `set_theme` stops following the system. Call `follow_system_theme()` again to go back.
- A custom theme made from `dark_theme()` keeps dark-theme colors in every role you leave alone.
  Check the text against its background if you change surfaces.
- `load_image` returns an error rather than throwing: check it before calling `symbolic`.
