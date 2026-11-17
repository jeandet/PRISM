# Python reference

Every public name in the `prism` module, grouped by job. Each entry says what it is, gives a short
example and names the C++ type underneath. Read [Python](python.md) first for the ideas.

Thread rules in one place:

- **Any thread** may assign `.value`, call `observe`, `send`, `push` and the other mutations: they
  are posted to the logic thread.
- **Reads** (`.value`, `size()`, `rows()`...) ask the logic thread and wait for the answer.
- **Callbacks** (observers, `on_change` methods, derived functions, `on_choice`) run on the logic
  thread.

## The model

### prism.Model

The base class of every model. Fields are class attributes; each instance gets its own:

```python
class Counter(prism.Model):
    count = prism.field(0)

    def view(self, vb):          # optional: without it, fields are stacked in order
        vb.widget(self.count)
```

A class attribute annotated with `typing.Annotated[int | float | str | bool, ...]` and a plain
default becomes a field with that validation (needs pydantic):

```python
class Settings(prism.Model):
    gain: Annotated[int, PField(ge=0, le=100)] = 50
```

### Handles

`m.count` is a *handle*. Every value handle has:

| Member | What it does |
|---|---|
| `.value` | Read or assign the value. |
| `get()`, `set(v)` | The same as reading and assigning `.value`. |
| `observe(callback)` | `callback(value)` runs on each change. Returns a `Connection`. |
| `add(n)` | Plain number fields: adds `n` on the logic thread. Safe from many threads. |

`prism.Connection.disconnect()` ends an observer early. A `Connection` is also a context manager:
the observer ends with the `with` block. Otherwise it lives as long as the handle or its model.

## Fields with no widget of their own

| Function | C++ | `.value` | Shown as |
|---|---|---|---|
| `field(default, validator=None, *, tooltip=None)` | `Field<int>`, `Field<double>`, `Field<std::string>`, `Field<bool>` | the value | Its text (a `bool`: a checkbox). Not an input. |
| `shared(default, validator=None, *, tooltip=None)` | `Shared<T>` | the latest value | Its text. |
| `channel(type_hint=0)` | `Channel<T>` | (none: `send(v)`) | Nothing. |
| `derived(fn, *deps, type_hint=None, tooltip=None)` | `Derived<T>` | the computed value, read-only | Its text. |
| `list_field(default=None)` | `List<T>` | (see below) | A list, with `vb.list`. |

```python
class Board(prism.Model):
    total = prism.field(0)
    latest = prism.shared(0.0)       # any thread; only the newest value is kept
    events = prism.channel("")       # any thread; every value, in order
    double = prism.derived(lambda self: self.total.value * 2)
    names = prism.list_field(["ada"])
```

- `shared`: writes apply at once, from any thread. If they come faster than the screen, the ones
  in between are dropped.
- `channel`: `m.events.send(v)` from any thread; `m.events.observe(cb)` gets every value, in
  order. `type_hint` sets the element type: `0`, `0.0`, `""` or `False`.
- `derived`: with no `deps` it follows the fields the function reads. `derived(fn, "a", "b")` or
  `derived(fn, a, b)` lists them instead. `type_hint` gives the type when the first value cannot
  tell (`type_hint=0.0`). Used as `@prism.derived("a")` on a method, the method computes it.
- `list_field`: elements are all `int`, all `float` or all `str`.

List handle:

| Member | What it does |
|---|---|
| `push(v)`, `erase(i)`, `set(i, v)`, `replace_all(values)` | Each is one atomic change, from any thread. |
| `size()`, `get(i)`, `to_list()` | Reads. |
| `observe_insert(cb)` | `cb(index, value)` on each insert. |
| `observe_remove(cb)` | `cb(index)` on each removal. |
| `observe_update(cb)` | `cb(index, value)` on each change of a row. |
| `selected` | The selected row, or `None` (with `vb.list(h, selectable=True)`). Assignable. |
| `observe_selected(cb)` | `cb(row)` when the selection changes. |

## Input widgets

Each takes `tooltip="..."` as a keyword.

| Function | C++ | `.value` |
|---|---|---|
| `slider(default, min=0.0, max=1.0, validator=None, *, orientation="horizontal", step=0.0, label="", unit="")` | `Field<Slider<double>>` | the number |
| `checkbox(default, label=None, validator=None)` | `Field<Checkbox>` | `True` or `False` |
| `radio(default, options, validator=None)` | `Field<RadioGroup>` | the chosen option |
| `choice(options, default=None, placeholder="")` | `Field<Choice>` | the chosen option, or `None` |
| `combobox(default="", options=(), placeholder="", validator=None)` | `Field<ComboBox>` | the text, typed or picked |
| `spinbox(default, min=0, max=100, step=1, validator=None)` | `Field<SpinBox<int64_t>>` or `<double>` | the number, kept in range |
| `text_area(default="", rows=6, read_only=False, placeholder="")` | `Field<TextArea<>>` | the text |
| `button(text, icon=None, *, shortcut="", checked=None)` | `Field<Button>` | the click count |
| `link(text, url="")` | `Field<Link>` | the click count |
| `color(value="#000000")` | `Field<Color>` | `"#RRGGBB"` |
| `date(value)` | `Field<year_month_day>` | `"YYYY-MM-DD"` |
| `time_of_day(value="00:00")` | `Field<TimeOfDay>` | `"HH:MM"` |

```python
class Form(prism.Model):
    volume = prism.slider(60, min=0, max=100, step=1, label="Volume", unit="%")
    notify = prism.checkbox(True, label="Notify me")
    size = prism.radio("M", options=["S", "M", "L"])
    port = prism.choice(["COM1", "COM3"], placeholder="Port")
    city = prism.combobox("", options=["Paris", "Lyon"], placeholder="City")
    copies = prism.spinbox(1, min=1, max=99)
    notes = prism.text_area("", rows=4, placeholder="Notes")
    save = prism.button("Save", shortcut="Ctrl+S")
    bold = prism.button("Bold", checked=False)       # a toggle
    docs = prism.link("Documentation", url="https://prism-gui.readthedocs.io")
    accent = prism.color("#3366cc")
    due = prism.date("2026-12-01")
    alarm = prism.time_of_day("07:30")
```

Extra members:

| Handle | Member | Meaning |
|---|---|---|
| slider | `range` | `(min, max)`; assign a tuple to change it. |
| slider | `label`, `unit`, `step` | Read-only. |
| choice, combobox | `options` | The list; assignable. A choice no longer listed becomes `None`. |
| button | `checked` | A toggle's state, or `None` for a plain button. Assignable. |
| button | `text`, `has_icon` | Read-only. |
| link | `url` | Assignable. |

Notes:

- A slider accepts a `.value` outside its range; only a drag clamps.
- `radio`, `choice`, `color`, `date`, `time_of_day` raise `ValueError` for a value they cannot
  hold.
- A button's or link's `observe` runs on each click. A `shortcut` presses the button from
  anywhere in the window. `icon` is an image file (PNG, JPEG, SVG).

## Display widgets

| Function | C++ | `.value` |
|---|---|---|
| `label(text="", heading=False, *, wrap=False)` | `Field<Label<>>` | the text |
| `progress(default=0.0, min=0.0, max=1.0, label="", validator=None, *, busy=False)` | `Field<ProgressBar>` | the progress |
| `picture(path=None, *, width=None, height=None, description="")` | `Field<Picture>` | |

```python
class Status(prism.Model):
    title = prism.label("Export", heading=True)
    bar = prism.progress(0.0, label="Exporting")
    logo = prism.picture("logo.svg", width=64, height=64, description="The logo")
```

- `progress.busy = True` shows a moving segment when the length is unknown.
- `picture.load(path)` shows another file; `description` is read by assistive tech.

## Containers and popups

| Function | Placed with | `.value` |
|---|---|---|
| `tabs(selected=0)` | `vb.tabs(h, [(title, fn), ...])` | the selected page's index |
| `expander(title, open=False)` | `vb.section(h, fn)` | `True` while open |
| `popup(modal=True)` | `vb.popup(h, fn)` | whether it is open (also `.open`) |
| `dialog(title, message="", buttons=("OK",))` | `vb.dialog(h, content=None)` | whether it is open (also `.open`) |
| `menu(title, items)` | `vb.widget(h)` (menus side by side make a menu bar) | the number of choices |
| `context_menu(items)` | `vb.context_menu(h)` | the number of choices |
| `dock_layout(default)` | `vb.dock(h, [(name, title, fn), ...])` | the layout, as nested dicts |

Each `fn` receives the `ViewBuilder` and places the page's widgets.

```python
class App(prism.Model):
    pages = prism.tabs()
    more = prism.expander("Advanced")
    confirm = prism.dialog("Delete?", "This cannot be undone.", buttons=("Delete", "Cancel"))
    file_menu = prism.menu("&File", ["Open", {"text": "&Save", "shortcut": "Ctrl+S"}, "-", "Quit"])
    level = prism.slider(0.5)

    def view(self, vb):
        vb.widget(self.file_menu)
        vb.tabs(self.pages, [
            ("Main", lambda vb: vb.widget(self.level)),
            ("About", lambda vb: None),
        ])
        vb.section(self.more, lambda vb: vb.widget(self.level))
        vb.dialog(self.confirm)

m = App()
m.file_menu.on_choice(lambda label: print("chose", label))
m.confirm.on_choice(lambda button: print("answer", button))
```

Menu items: a string, `"-"` for a separator, or a dict
`{"text": "&Save", "shortcut": "Ctrl+S", "enabled": True, "checked": None, "icon": "save.svg",
"items": [...]}` (`items` opens a submenu). `&` before a letter makes Alt plus that letter open
it.

Dialog: set `.open = True` to ask. `on_choice(cb)` gets the button's text; `.chosen` is the last
answer, `None` after Escape.

Dock layouts are built from:

| Function | What it makes |
|---|---|
| `dock_group(*panels, active=0)` | Panels, by name, as tabs; `active` shown. |
| `dock_split(*parts, vertical=False, sizes=None)` | Groups or splits side by side, or stacked if `vertical`. |
| `dock_layout(default)` | The field holding the layout. `.floating` lists the panels in windows of their own. |

```python
layout = prism.dock_layout(prism.dock_split(prism.dock_group("files"),
                                            prism.dock_group("editor", "log"), sizes=[0.3, 0.7]))
```

## Data widgets

### Tables

`table_field(source)` shows any object with these methods:

```python
class People(prism.TableSource):
    rows = [["ada", "36"], ["alan", "41"]]
    def column_count(self): return 2
    def row_count(self): return len(self.rows)
    def cell_text(self, row, col): return self.rows[row][col]
    def header(self, col): return ["Name", "Age"][col]          # optional
    def set_cell(self, row, col, text):                         # optional: makes cells editable
        self.rows[row][col] = text

class App(prism.Model):
    table = prism.table_field(People())
    def view(self, vb):
        vb.table(self.table, sort_column=0)
```

| Source method | Meaning |
|---|---|
| `column_count()`, `row_count()`, `cell_text(row, col)` | Required. |
| `header(col)` | The column's title. |
| `set_cell(row, col, text)` | Edits are allowed (double-click or F2). Return `False` to refuse the text. |
| `editable(col)` | Limits editing to some columns. |
| `cell_check(row, col)` | A checkbox in that cell: `True`, `False`, or `None` for none. |
| `cell_image(row, col)` | An icon's file path, or `None`. |

Handle: `refresh()` after the data changed; `columns` lists the columns shown, by index, in order
(`[]`: all). `vb.table(h, sort_column=None, descending=False)` places it.

### Trees

`tree_field(source)` shows a tree. Ids are any integers the source chooses:

| Source method | Meaning |
|---|---|
| `root_count()`, `root_at(i)` | The top-level nodes. |
| `child_count(id)`, `child_at(id, i)` | A node's children. |
| `label(id)`, `has_children(id)` | Its text, and whether it unfolds. |
| `attributes(id)` | A dict shown in the detail pane. Optional. |
| `icon(id)` | A glyph, or `None`. Optional. |
| `checked(id)`, `set_checked(id, on)` | A checkbox per node (`None`: none). Optional. |
| `rename(id, name)` | F2 or a double-click renames; return `False` to refuse. Optional. |

Handle: `refresh()`, `selected` (an id or `None`; assignable), `selection` (every selected id),
`observe_selection(cb)`, `rows()` (the visible rows as `TreeRow(label, depth, has_children,
expanded, selected)`). `prism.TreeHandle(source)` is a standalone tree, for tests.

### Plots

```python
class Scope(prism.Model):
    plot = prism.plot_field()
    def view(self, vb):
        vb.canvas(self.plot)

m = Scope()
m.plot.replace_series([0, 1, 2, 3], [0, 1, 4, 9], color="#33cc66")
m.plot.set_labels(x="time (s)", y="value")
```

| Member | What it does |
|---|---|
| `add_series(xs, ys, color="", thickness=2.0, fill=False)` | Adds a series. `color` is `"#rrggbb"`. |
| `replace_series(xs, ys)` or `replace_series([(xs, ys, color), ...])` | Replaces every series in one step. |
| `clear_series()` | Removes them all. |
| `set_labels(x=None, y=None)`, `x_label`, `y_label` | The axes' titles. |
| `reset_view()` | Fits the view to the data. |
| `series_count()`, `series_len(i)` | Reads. |
| `notify()` | Redraw after changing series in place. |

`prism.PlotHandle()` is a standalone plot, for tests.

### Documents

```python
class Notes(prism.Model):
    doc = prism.document("# Hello\n\nSome *text*.", markdown=True)
    def view(self, vb):
        vb.text_editor(self.doc, rich=True)
```

| Member | What it does |
|---|---|
| `text` | The whole text. |
| `set_text(text, markdown=False)` | Loads new text (no undo back). |
| `to_markdown()` | The document as Markdown. |
| `find_all(needle, case_sensitive=True, whole_word=False)` | Every match, as `(start, end)` byte offsets. |
| `replace_all(needle, with_, case_sensitive=True, whole_word=False)` | Replaces every match as one undo step; returns how many. |
| `observe(cb)` | `cb()` after each change. |

`prism.has_markdown` is `True` when PRISM was built with Markdown support.

## The ViewBuilder

`view(self, vb)` receives it. Container functions take either handles or one function:

| Call | What it does |
|---|---|
| `widget(h, enabled=None, drag=None, drop=None, accepts=None)` | Places a handle's widget. `enabled`: a bool field or checkbox that greys it out while false. `drag`: a function giving the object a drag carries. `drop`: `fn(value, x, y)` for objects of type `accepts` dropped here. |
| `hstack(h1, h2, ...)` or `hstack(fn)` | Side by side. |
| `vstack(h1, h2, ...)` or `vstack(fn)` | Stacked. |
| `group(title, fn)` | Outlined, under a title. |
| `toolbar(fn)`, `status_bar(fn)` | A row of tools; a row of status texts. |
| `enabled_if(flag, fn)` | Greys out what `fn` places while `flag` is false. |
| `list(h, reorderable=False, selectable=False, follow_tail=False)` | A list. `follow_tail` keeps the newest row in sight, like a terminal. |
| `table(h, sort_column=None, descending=False)` | A table. |
| `tree(h)` | A tree. |
| `canvas(h)` | A plot. |
| `tabs(h, pages)`, `section(h, fn)`, `popup(h, fn)`, `dialog(h, content=None)`, `context_menu(h)`, `dock(h, panels)` | See *Containers and popups*. |
| `text_editor(h, rich=False, read_only=False, rows=0)` | Edits a document. `rich`: Ctrl+B/I/E style the selection, `# ` and `- ` start a heading or a list item. `rows=0` fills the space. |
| `code_editor(h, language="", rows=0)` | Edits a document as code: `"python"`, `"cpp"`, `"sql"`, `"json"`, `"toml"`, `"shell"`. |
| `text_view(h, rows=0)` | Shows a document, read-only. |

## Running

| Function | What it does |
|---|---|
| `run(model, title="PRISM App", headless_seconds=None, until=None)` | Opens the window and blocks until it closes. With `headless_seconds`, runs without a display for that long, or until `until()` is true. |
| `headless(model, *, timeout=10.0)` | A context manager running the model without a display; yields an `App`. |
| `App.wait_until(predicate, timeout=None, poll=0.005)` | Waits until `predicate()` is true. `TimeoutError` after `timeout`. Not from an observer. |
| `App.quit()`, `App.is_running` | Ends the app; whether it runs. |
| `transaction()` | `with prism.transaction():` the writes inside are applied together, with one notification. No rollback. |
| `on_change(*deps, immediate=False)` | Method decorator: runs the method when one of `deps` changes. |
| `worker(fn, *, interval=None, repeat=None, daemon=True, name=None)` | Runs `fn` on its own thread; returns a `Worker`. |
| `on_error(handler)` | `handler(exception)` gets errors from observers, derived functions and workers. `None`: print them. |
| `is_logic_thread()` | Whether the caller runs on the logic thread. |
| `validator_for(type_hint)` | A validator from an `Annotated` type, for `field(validator=...)`. |

`worker`: `fn` takes no argument or a `stop` event. Without `interval` it runs once. With
`interval` it waits that long before each call; with `repeat` it stops after that many calls.
`Worker.stop()` ends it; `is_alive` tells; a `Worker` is also a context manager. Workers still
running stop when `run` returns.

## While an app runs

These need a running app; call them from any thread.

| Function | What it does |
|---|---|
| `set_theme(name)` | `"light"`, `"dark"` or `"system"`. |
| `open_window(model, title="PRISM", width=480, height=360)` | Shows another model in a window of its own (desktop only). |
| `choose_files(on_done, kind="open", title="", filters=[], start="")` | The system's file dialog. `on_done(paths, error)` on the logic thread. `kind`: `"open"`, `"open_many"`, `"save"`, `"folder"`. `filters`: `[("Text", "txt;md")]`. |
| `show_message(text, title="", kind="info")` | The system's message box: `"info"`, `"warning"` or `"error"`. |
| `on_file_drop(callback)` | `callback(paths, x, y)` for files dropped on the window. Set it before or during `run`. |
| `settings_dir(org, app)` | The folder for this user's settings (no app needed). Save your own files there. |

## Pitfalls

- There is no single-line text input: use `combobox` with no options, or `text_area(rows=1)`.
- `m.f.value += 1` from a worker can lose an update. Use `m.f.add(1)`.
- An observer ends when its handle and model are gone, even if you kept the `Connection`.
