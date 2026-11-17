# Python SDK

The same MVB model from Python, and any thread may mutate it:

```python
import prism
from typing import Annotated
from pydantic import Field as PField

Vol = Annotated[float, PField(ge=0, le=1)]

class Mixer(prism.Model):
    volume: Vol = 0.75                          # Annotated: validated by pydantic
    mute = prism.checkbox(False, label="Mute")
    count = prism.field(42)
    volume_slider = prism.slider(0.75, min=0.0, max=1.0)
    total = prism.derived(lambda self: self.count.value * 2)   # follows what it reads

    def view(self, vb):                         # optional, else fields are stacked
        vb.hstack(self.volume_slider, self.mute)
        vb.widget(self.count)

m = Mixer()
prism.run(m, title="Mixer")                     # blocks; releases the GIL around the event pump

# from any thread:
m.count.value = 43
conn = m.count.observe(lambda v: print(v))
with prism.transaction():                       # one publish for both changes
    m.count.value = 1
    m.volume = 0.9
```

<details>
<summary><b>Python ↔ C++ mapping</b></summary>

| Python | C++ | Notes |
|---|---|---|
| `prism.field(default)` | `Field<T>` | `.value` get/set, `.observe()` |
| `prism.slider` / `prism.checkbox` | `Field<Slider<>>` / `Field<Checkbox>` | widget types |
| `prism.radio(default, options=[...])` | `Field<RadioGroup>` | `.value` is the chosen option string |
| `prism.spinbox(default, min, max, step)` | `Field<SpinBox<int64_t>>` / `Field<SpinBox<double>>` | int when `default` and `step` are ints |
| `prism.progress(default, label=...)` | `Field<ProgressBar>` | display only; `busy=True` for unknown length |
| `prism.button(text, icon=path, shortcut="F5", checked=None)` | `Field<Button>` | `.value` counts clicks; `checked=False` makes a toggle (`.checked`) |
| `prism.choice(options, placeholder=)` | `Field<Choice>` | `.value` is the option or None; `.options` can change |
| `prism.link(text, url=)` | `Field<Link>` | opens the URL; `.value` counts clicks |
| `prism.color("#RRGGBB")` | `Field<Color>` | a picker; `.value` is `"#RRGGBB"`, the alpha is kept |
| `prism.date("YYYY-MM-DD")` | `Field<std::chrono::year_month_day>` | a month calendar; `.value` is ISO text |
| `prism.time_of_day("HH:MM")` | `Field<ui::TimeOfDay>` | hour and minutes stepped or typed; `.value` is `"HH:MM"` |
| `prism.picture(path, width=, height=)` | `Field<Picture>` | `.load(path)` shows another file |
| `prism.expander(title)` + `vb.section(h, fn)` | `Field<Expander>` + `vb.section` | content built only while open |
| `vb.toolbar(fn)`, `vb.status_bar(fn)`, `vb.group(title, fn)` | the same | containers with a look |
| `prism.combobox(default, options=[...])` | `Field<ComboBox>` | free text with suggestions |
| `prism.popup(modal=True)` + `vb.popup(h, fn)` | `Field<Popup>` + `ViewBuilder::popup` | `.open` shows it |
| `prism.menu("&File", items)` | `Field<Menu>` | items: `"Open"`, `"-"`, or `{"text", "shortcut": "Ctrl+S", "enabled", "checked", "icon"}`; `.on_choice(cb)` gets the label |
| `prism.context_menu(items)` + `vb.context_menu(h)` | `Field<ContextMenu>` | right click in the container |
| `vb.widget(h, enabled=flag)`, `vb.enabled_if(flag, fn)` | `.enabled(flag)`, `enabled_if` | flag: a bool field, derived bool or checkbox |
| `prism.set_theme("light" / "dark" / "system")` | `AppContext::set_theme`, `follow_system_theme` | any thread, while an app runs |
| `vb.widget(h, drag=fn, drop=fn, accepts=Type)` | `.drag_source`, `.drop_target` | `drag()` gives any Python object; `drop(value, x, y)` |
| `vb.list(h, reorderable=True)` | `vb.list(items).reorderable()` | rows dragged into another order |
| `vb.list(h, selectable=True)`, `h.selected` | `vb.list(items).selection(field)` | a click or Up/Down/Home/End selects; `None`: no row |
| `prism.dock_layout(prism.dock_split(prism.dock_group("a"), ...))` + `vb.dock(h, [(name, title, fn), ...])` | `Field<DockLayout>` + `vb.dock` | `.value` is the layout as nested dicts |
| `prism.label(text, heading=False, wrap=False)` | `Field<Label<>>` | display only; `wrap` breaks it into lines |
| `prism.text_area(text, rows=6, read_only=False)` | `Field<TextArea<>>` | read-only: selectable, UI font |
| `prism.tabs()` + `vb.tabs(h, [(title, fn), ...])` | `Field<TabBar<>>` + `vb.tabs` | `.value` is the selected page |
| `prism.dialog(title, message, buttons)` + `vb.dialog(h, content=None)` | `Field<Dialog>` + `vb.dialog` | `.open = True` asks; `.on_choice(cb)` gets the button |
| `prism.table_field(source)` + `vb.table(h, sort_column=None)` | `vb.table(ColumnStorage)` | source: `column_count`, `row_count`, `cell_text`, `header`; `.refresh()` after a change; `set_cell(row, col, text)` makes it editable; `cell_check`/`cell_image` add boxes and icons; `h.columns` hides or reorders |
| `prism.tree_field(source)` + `vb.tree(h)` | `vb.tree(TreeController&)` | source: `root_count`, `root_at`, `child_count`, `child_at`, `label`, `has_children`; `checked`/`set_checked` add boxes, `rename` lets F2 rename; `h.selected`, `h.selection`, `h.observe_selection` |
| `prism.open_window(model, title=, width=, height=)` | `AppContext::open_window` | while an app runs; desktop |
| `prism.settings_dir(org, app)` | `backends::settings_dir` | save the app's settings there (json, tomllib) |
| `prism.shared(default)` | `Shared<T>` | latest value, cross-thread |
| `prism.channel(type_hint)` | `Channel<T>` | lossless, ordered |
| `prism.derived(fn)` | `Derived<T>` | follows what `fn` reads; `prism.derived(fn, *deps)` lists them |
| `prism.list_field([...])` | `List<T>` | `push` / `erase` / `observe_*` |
| `prism.document(text, markdown=False)` + `vb.text_editor` / `code_editor(h, "python")` / `text_view` | `text::Document` + the same | `.text`, `set_text`, `to_markdown`, `find_all`, `replace_all`, `observe` |
| `prism.transaction()` | `prism::transaction([]{...})` | coalesced publish |
| `prism.run(model)` | `app::run(model, {.title})` | |

Also: `prism.headless()` and `prism.run(m, headless_seconds=..., until=...)` for tests and CI,
`field.add(n)` for atomic increments, `prism.choose_files(on_done, kind="open")`,
`prism.show_message(text)` and `prism.on_file_drop(callback)` for the platform's file dialogs,
message boxes and drag and drop, a `tooltip="..."` keyword on every field factory
(`ViewBuilder::tooltip_for` in C++), `plot.replace_series()` / `set_labels()`, and the
`@prism.on_change(*deps)` method decorator.

</details>

```bash
pip install -e . --no-build-isolation
PYTHONPATH=builddir/python python python/examples/01_counter.py
```

[`python/examples/`](../../python/examples/README.md) has a dozen runnable examples, from a counter to
worker-pool plots and an asyncio bridge. Its README also spells out the
[threading guarantees](../../python/examples/README.md#threading-guarantees).
The [Python SDK design doc](../design/python-sdk.md) covers the GIL, the posted-mutation queue
and object lifetimes.
