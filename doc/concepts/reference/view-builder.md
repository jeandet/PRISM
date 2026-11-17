# ViewBuilder

`#include <prism/prism.hpp>` · namespace `prism::app`

`ViewBuilder` is what a `view()` method receives. Each call places a widget or opens a container
at the current position. [Views and layout](../views.md) shows how to use it; this page lists every
call.

```cpp
// open, save: buttons; files, body: text areas; title, status: labels -- fields of the struct
void view(app::ViewBuilder& vb) {
    vb.toolbar([&] { vb.widget(open); vb.widget(save); });
    vb.hstack([&] {
        vb.widget(files);
        vb.handle();
        vb.vstack([&] { vb.widget(title); vb.widget(body); });
    });
    vb.status_bar([&] { vb.widget(status); });
}
```

Containers take a function that places their children. The function runs right away, inside the
container.

## Placing values

| Call | Places |
|---|---|
| `widget(field)` | the field's widget; returns a handle (below) |
| `widget(derived)`, `widget(shared)` | read-only text that follows the value |
| `widget_readonly(field)` | the field's widget, drawn but deaf to input |
| `vstack(a, b, ...)`, `hstack(a, b, ...)` | several values at once: fields, `Derived`s, `Shared`s, blocks. `hstack` opens a row; `vstack` places them in the **current** container |
| `block(s)` | a nested struct, with its own `view()`, or (with reflection) as a column of its fields |
| `tooltip_for(value, text)` | the tooltip a value's widget gets wherever it is placed later |

### The widget handle

`widget(field)` returns a handle. Chain its calls in the same statement:

```cpp
// save: a Field<ui::Button>; has_name: a Derived<bool>
vb.widget(save).tooltip("Write the file").enabled(has_name);
```

| Call | Meaning |
|---|---|
| `.tooltip(text)` | help shown when the pointer rests on it |
| `.enabled(flag)` | greyed out and deaf to input while `flag` (a bool `Field` or `Derived`) is false |
| `.focus_on_open()` | inside a popup or dialog, this widget has the focus when it opens |
| `.drag_source(make)` | pressing and moving drags the value `make()` returns |
| `.drop_target(on_drop)` | takes dropped values of the type `on_drop(const T&, DropPlace)` takes |

Drag and drop is described in [Drag and drop](../../widgets/drag-and-drop.md).

## Rows, columns and space

| Call | Meaning |
|---|---|
| `hstack(fn)` | a row: children left to right |
| `vstack(fn)` | a column: children top to bottom |
| `spacer()` | takes the space left along the row or column |
| `gap(points)` | a fixed space |
| `handle()` | a divider the user drags to resize its neighbours (a split) |
| `remember_split(sizes)` | the current row or column's split sizes live in a `Field<ui::SplitSizes>` |
| `layers(places, fn)` | children over one another: the first fills the area, each later one lies at the rectangle `places()` returns for it |

## Frames

| Call | Looks like | Lays out |
|---|---|---|
| `toolbar(fn)` | a band on the window's chrome color | a row |
| `status_bar(fn)` | a band of short texts | a row |
| `group("Title", fn)` | an outline under a title | a column |
| `card(fn)` | a raised surface, outlined | a column |
| `section(expander, fn)` | a header that folds; `fn(ViewBuilder&)` builds the content only while open | a column |

A block placed in a toolbar or status bar lays its widgets along the bar.

## Scrolling and state

| Call | Meaning |
|---|---|
| `scroll(fn)` | a scroll area; the bar shows when needed |
| `scroll(policy, fn)` | the same with `ScrollBarPolicy::Auto`, `Always` or `Never` |
| `scroll(field, fn)` | the position lives in a `Field<ui::ScrollArea>` the app reads and sets |
| `enabled_if(flag, fn)` | everything `fn` places is greyed out while `flag` is false; adds no container |
| `keep_as(key, fn)` | what `fn` places keeps its widget state when rebuilt elsewhere under the same key |

## Widgets with a call of their own

These widgets are not one field. Each is documented with its widget.

| Call | Widget | See |
|---|---|---|
| `list(list, on_click)` | a virtual list over a `List<T>`; `.selection(f)`, `.reorderable()`, `.follow_tail()` | [Data](../../widgets/data.md) |
| `table(data)` | a table over columns or a `List` of structs; `.headers(...)`, `.sort(col)`, `.columns(f)`, `.depends_on(...)` | [Data](../../widgets/data.md) |
| `tree(controller, detail)` | a tree, with a detail pane unless `detail` is false | [Data](../../widgets/data.md) |
| `tabs(selected, fn)` with `tab(name, fn)` | tabs; only the shown page is built | [Layout](../../widgets/layout.md) |
| `tabs(selected, list, title, page)` | a tab per item of a `List`; `.closable()`, `.reorderable()` | [Layout](../../widgets/layout.md) |
| `pages(selected, fn)` | tabs without the header: the app picks the page | [Layout](../../widgets/layout.md) |
| `dock(layout, panels)` | panels in splits and tab groups, dragged by the user | [Docking](../../widgets/docking.md) |
| `canvas(model)` | your own drawing; `.min_size(...)`, `.depends_on(...)` | [Plot and canvas](../../widgets/plot-and-canvas.md) |
| `text_view(doc)`, `text_editor(doc)` | a long document, shown or edited | [Documents](../../widgets/documents.md) |
| `code_editor(doc, lang)`, `code_view(doc, lang)` | code, edited or shown, in colors | [Documents](../../widgets/documents.md) |
| `dialog(field, fn)` | a modal dialog | [Menus and dialogs](../../widgets/menus-and-dialogs.md) |
| `popup(field, fn)` | a popup holding widgets | [Menus and dialogs](../../widgets/menus-and-dialogs.md) |
| `context_menu(field)` | a menu on right click in the current container | [Menus and dialogs](../../widgets/menus-and-dialogs.md) |

## depends_on

A table or canvas that reads values the view does not place says so:

```cpp
// scope: a struct with a canvas() method; timebase, trigger: fields it reads
vb.canvas(scope).depends_on(timebase, trigger);
```

The canvas redraws when one of them changes, and the forgotten-field check counts them as placed.

## Pitfalls

- **`vstack(a, b)` does not open a column.** It places `a` and `b` in the current container: in a
  row, they line up sideways. `vstack([&] { ... })` opens a column.
- **Use a handle in the statement that made it.** The next placement may move the node it points to.
  The handles' calls only work on the temporary, so a stored handle does not compile.
- **Lambdas capture by reference** (`[&]`): they run inside the call.
  Functions that run later (a section's content, a tab's page, a popup's content) take a
  `ViewBuilder&` argument; use that one, not the outer `vb`.
