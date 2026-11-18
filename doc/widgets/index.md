# Widgets

A widget shows one field of your model and lets the user change it.
You never create a widget yourself.
You declare a field, and the type inside it picks the widget.

```cpp
struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};   // a slider
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};  // a checkbox
    core::Field<std::string> status{"Ready"};           // read-only text
};
```

When the user moves the slider, `volume` changes and everything that watches it runs.
When your code changes `volume`, the slider moves.
There is no second copy of the value to keep in step.

New to fields? Read [the model](../concepts/model.md) and [views](../concepts/views.md) first.
Every widget below runs live in the
[widget gallery](https://jeandet.github.io/PRISM/widget_gallery.html), in your browser.

## The families

::::{grid} 1 2 2 3
:gutter: 2

:::{grid-item-card} Controls
:link: controls
:link-type: doc
Buttons, checkboxes, sliders, text fields, choices, pickers, labels.
:::

:::{grid-item-card} Layout
:link: layout
:link-type: doc
Rows and columns, splits, groups, sections, scroll areas, tabs.
:::

:::{grid-item-card} Data views
:link: data
:link-type: doc
Lists, tables, trees and node boards.
:::

:::{grid-item-card} Text and code
:link: text
:link-type: doc
Long documents, code editors, find and replace, completion.
:::

:::{grid-item-card} Markdown and rich text
:link: documents
:link-type: doc
Headings, lists and bold text, read from and written to Markdown.
:::

:::{grid-item-card} Plot and canvas
:link: plot-and-canvas
:link-type: doc
Scientific plots, and your own drawing.
:::

:::{grid-item-card} Menus and dialogs
:link: menus-and-dialogs
:link-type: doc
Menu bars, context menus, dialogs, popovers, tooltips, file dialogs.
:::

:::{grid-item-card} Docking
:link: docking
:link-type: doc
Panels the user rearranges, floats and closes.
:::

:::{grid-item-card} Drag and drop
:link: drag-and-drop
:link-type: doc
Typed values dragged from one widget to another.
:::

::::

## Which type gives which widget

| Field type | Widget | Page |
|---|---|---|
| `bool` | checkbox | [Checkbox](controls/checkbox.md) |
| `ui::Checkbox` | checkbox with a label | [Checkbox](controls/checkbox.md) |
| `ui::Button` | button, or a toggle with `.checked` | [Button](controls/button.md) |
| `ui::Slider<T>` | slider over a range | [Slider](controls/slider.md) |
| `ui::SpinBox<T>` | number with steppers | [Spin box](controls/spin-box.md) |
| `ui::TextField<>` | one line of text | [Text field](controls/text-field.md) |
| `ui::Password<>` | masked text | [Text field](controls/text-field.md) |
| `ui::TextArea<>` | several lines of text | [Text area](controls/text-area.md) |
| `ui::ComboBox` | text with suggestions | [Combo box](controls/combo-box.md) |
| any scoped `enum` | dropdown | [Choices](controls/choices.md) |
| `ui::Radio<E>` | radio buttons over an enum | [Choices](controls/choices.md) |
| `ui::RadioGroup` | radio buttons over strings | [Choices](controls/choices.md) |
| `ui::Choice` | dropdown over strings known at run time | [Choices](controls/choices.md) |
| `core::Color` | color picker | [Color](controls/color.md) |
| `std::chrono::year_month_day` | date picker | [Date and time](controls/date-and-time.md) |
| `ui::TimeOfDay` | time of day | [Date and time](controls/date-and-time.md) |
| `std::string`, numbers | read-only text | [Label](controls/label.md) |
| `ui::Label<>` | read-only text, a heading, wrapped text | [Label](controls/label.md) |
| `ui::Link` | a link that opens a URL | [Link](controls/link.md) |
| `ui::ProgressBar` | progress | [Progress bar](controls/progress-bar.md) |
| `ui::Picture` | an image | [Picture](controls/picture.md) |
| `ui::Expander` | the header of a folding section | [Groups and sections](layout/frames.md) |
| `ui::TabBar<>` | tabs | [Tabs and pages](layout/tabs.md) |
| `ui::Menu` | a menu title, and its menu | [Menu](popups/menu.md) |
| `ui::ContextMenu` | a right-click menu | [Context menu](popups/context-menu.md) |
| `ui::Dialog` | a modal dialog | [Dialog](popups/dialog.md) |
| `ui::Popup` | a popover that holds widgets | [Popover](popups/popup.md) |
| `ui::DockLayout` | a dock of panels | [Docking](docking.md) |

Collections are not fields: a `core::List<T>` is shown by [a list](data/list.md) or
[a table](data/table.md), and a tree source by [a tree](data/tree.md).
A `text::Document` is shown by [a text view or an editor](text.md).

Widgets match on concepts, not on exact types.
Your own string type works in a `Label<>` if it behaves like a string.
Any scoped enum gets a dropdown, without registering it.

```{toctree}
:maxdepth: 2
:hidden:

common
controls
layout
data
text
documents
plot-and-canvas
menus-and-dialogs
docking
drag-and-drop
```
