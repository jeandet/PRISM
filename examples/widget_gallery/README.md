# Widget Gallery

Every PRISM widget in one window: eight tabs under a menu bar. It is the place to try what the
framework does, on the desktop or [in the browser](https://jeandet.github.io/PRISM/widget_gallery.html).

| Tab | What to try |
|---|---|
| Controls | Type in the fields, pick a fruit in the combo box (or type your own), step the spin boxes with the arrows or the wheel, drag the slider and watch the progress bar follow, tick "busy". Rest the pointer on anything for its tooltip. The line at the bottom follows every change. |
| Catalog | Pick a widget in the tree: its page says what it is for, shows the C++ line that makes one (select and copy it), and has a live sample to try. Among them: the color, date and time pickers, a list whose selection follows the arrow keys, a tree with boxes, F2 rename and Ctrl/Shift-click selection, and drag and drop (drag a chip onto the drop zone). |
| Text | Edit the code with undo (Ctrl+Z). Click inside the Hebrew and Arabic words: the caret follows the right-to-left runs. Select the read-only text and copy it. |
| Data | Sort the 10,000-row table by clicking a header; drag a header to move its column, right-click for a menu that hides columns; double-click (or F2) a Note to edit it, tick a Watch box. Drag the handle between the panes, add items to the list and drag them into another order. |
| Plot | Drag the chart to pan, scroll to zoom, right-click to fit; the sliders change the curves, the spin box changes the canvas. |
| Popups | Open the dialogs (Enter, Escape, Tab), rename in a dialog holding a form, open the popover, right-click for a context menu, try the file dialogs and message box, drop a file on the window. |
| Docking | Drag a panel's tab: the guide's buttons put it in another panel's tabs, beside it, or along a whole side of the dock, or out of the window to float it in a window of its own (desktop). Type in the notes, pick a list row, change the controls: their state survives a move, and the arrangement is saved in `settings.toml`. |
| Build your own | Three live knobs from `knob.hpp` — drag one round, or focus one and hold an arrow key — then scroll the file they come from. Under it, the `ChannelStrip` struct and eight of them as a mixer: mute a channel and its meter dies, and the ninth knob is the master. |

Alt+F, Alt+V and Alt+H open the menus; View switches tabs.

## How it is built

Each tab is a plain model struct in its own header (`controls_tab.hpp`, `catalog_tab.hpp`, `text_tab.hpp`,
`data_tab.hpp`, `plot_tab.hpp`, `popups_tab.hpp`, `docking_tab.hpp`, `build_your_own_tab.hpp`);
`main.cpp` puts them in tabs under the menu bar.
Every tab uses the tiers that need no reflection (hand-written `view()`, table and tree sources),
so the same code builds with any C++23 compiler and with Emscripten. The Catalog is data: one
`Entry` per widget (name, category, doc, code, a function placing the sample); `vb.pages()` shows
the page of the entry selected in a `vb.tree(ctrl, /*detail=*/false)`.

The last tab is the framework's own recipe, result first and code under it. `knob.hpp` is 163 lines,
comments and all: a `Knob` value type, its geometry as free functions, and one `Widget<Knob>`
specialization — `record()` draws the dial, its arc and its pointer with `DrawList`,
`handle_input()` points the hand at the pointer while you drag, and turns it by the arrow keys. There is no base
class and nothing to register; `#embed` puts that exact file in the page, so the code and the
knobs above it cannot disagree. `ChannelStrip` is the reuse tier: a struct of four `Field`s and a
`view()`, so `std::array<ChannelStrip, 8>` is a mixer and its level meter is the strip's own
`canvas()`.

The icons are SVG files in `icons/`, decoded by `render::load_image` (bundled into the page on the
web) and marked `render::symbolic`, so they take the color of the text beside them on either
theme. Reactivity is ordinary `on_change()` wiring in each tab's constructor: the Controls summary
line and the progress bar follow the other fields. On the Controls tab, Save stays greyed out
until a name is typed, and "Lock the settings" greys out the group below it. The Theme dropdown
switches between System (the OS's choice), Light and Dark; it and the window size are remembered
in `settings.toml` (`app::SettingsFile`).

```bash
./builddir/examples/widget_gallery/widget_gallery            # the app
./builddir/examples/widget_gallery/widget_gallery out.svg 1  # a capture of the Catalog tab
./builddir/examples/widget_gallery/widget_gallery out.svg 7  # ... and of the Build your own tab
```
