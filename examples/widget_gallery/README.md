# Widget Gallery

Every PRISM widget in one window: five tabs under a menu bar. It is the place to try what the
framework does, on the desktop or [in the browser](https://jeandet.github.io/PRISM/widget_gallery.html).

| Tab | What to try |
|---|---|
| Controls | Type in the fields, pick a fruit in the combo box (or type your own), step the spin boxes with the arrows or the wheel, drag the slider and watch the progress bar follow, tick "busy". Rest the pointer on anything for its tooltip. The line at the bottom follows every change. |
| Text | Edit the code with undo (Ctrl+Z). Click inside the Hebrew and Arabic words: the caret follows the right-to-left runs. Select the read-only text and copy it. |
| Data | Sort the 10,000-row table by clicking a header, open the tree's branches, add items to the list. |
| Plot | Drag the chart to pan, scroll to zoom, right-click to fit; the sliders change the curves, the spin box changes the canvas. |
| Popups | Open the dialogs (Enter, Escape, Tab), rename in a dialog holding a form, open the popover, right-click for a context menu, try the file dialogs and message box, drop a file on the window. |

Alt+F, Alt+V and Alt+H open the menus; View switches tabs.

## How it is built

Each tab is a plain model struct in its own header (`controls_tab.hpp`, `text_tab.hpp`,
`data_tab.hpp`, `plot_tab.hpp`, `popups_tab.hpp`); `main.cpp` puts them in tabs under the menu bar.
Every tab uses the tiers that need no reflection (hand-written `view()`, table and tree sources),
so the same code builds with any C++23 compiler and with Emscripten.

The icons are SVG files in `icons/`, decoded by `render::load_image` (bundled into the page on the
web). Reactivity is ordinary `on_change()` wiring in each tab's constructor: the Controls summary
line and the progress bar follow the other fields.

```bash
./builddir/examples/widget_gallery/widget_gallery            # the app
./builddir/examples/widget_gallery/widget_gallery out.svg 1  # a capture of the Text tab
```
