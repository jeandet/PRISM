# Tour: five small programs, in order

Each step is one standalone program: it builds, runs, and ends by pointing at the big example that
does the same thing for real. Read them in order — 01 to 05 — or start at the step whose idea you
are missing.

Build them like any other example:

```bash
meson setup builddir && ninja -C builddir
./builddir/examples/tour/01_struct_to_ui
```

## 01. A struct is a window

A struct of `core::Field`s is already a window: reflection reads the members, and each one shows up
as the widget its type names — a `TextField<>` for text, a `Slider<>` for a number, a `Checkbox` for
a flag. There is no `view()` to write and no layout to describe. Without reflection (the web build)
the same fields are placed by hand, and the widgets are identical.

```cpp
struct Settings {
    core::Field<ui::TextField<>> name{{.value = "PRISM"}};
    core::Field<ui::Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 1.0, .label = "Volume"}};
    core::Field<ui::Checkbox> mute{{.checked = false, .label = "Mute"}};

#if !__cpp_impl_reflection
    // Without reflection (the web build) the fields are placed by hand; the widgets are the same.
    void view(app::ViewBuilder& vb) { vb.vstack(name, volume, mute); }
#endif
};
```

Next: `examples/model_lab_bench/` builds a whole instrument out of structs like this one.

## 02. Derived values

A `core::Derived` is a value computed from other fields. It notes which fields its function
reads and recomputes when one of them changes: there is no list to write and no callback to
remember to call. Here the area follows the two spin boxes, and `vb.widget` shows it as a
read-only label like any other value.

```cpp
    core::Field<ui::SpinBox<int>> width{{.value = 2, .min = 1, .max = 99}};
    core::Field<ui::SpinBox<int>> height{{.value = 3, .min = 1, .max = 99}};

    // Recomputes whenever either side changes; nothing has to call it, and a widget can show it.
    core::Derived<std::string> area{[this] {
        return "area: " + std::to_string(value(width) * value(height));
    }};
```

Next: `examples/model_synth_rack/model_synth_rack.cpp` — its `Link` is one `Derived` over six blocks.

## 03. A worker thread

Work that must not run on the UI thread writes into a `core::Shared<T>` and says no more. The logic
thread collects the newest value with `ctx.receive`, and only there may it touch the model. Because
`Shared<T>` coalesces, a slow UI drops intermediate values instead of queueing them.

```cpp
    std::jthread worker{[this](std::stop_token stop) {
        for (int n = 0; !stop.stop_requested(); ++n) {
            ticks = n;   // Shared<T>: any thread may set it
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }};

    void attach(app::AppContext& ctx) {
        ctx.receive(ticks, [this](int n) { set_value(reading, "tick " + std::to_string(n)); });
    }
```

Next: `examples/model_system_monitor/` polls `/proc` the same way, with a `Channel` of events beside.

## 04. Your own widget

A type brings its own widget with one `Widget<T>` specialization: `record` draws it into the draw
list, `handle_input` reacts to events, and `focus_policy` says how it can be reached. Nothing is
registered — a `Field<Level>` is now a bar that the tab key reaches and the arrow keys move.

```cpp
template <>   // one specialization, and Level works anywhere a Field<T> does
struct Widget<Level> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::tab_and_click;
    static void record(DrawList& dl, const Field<Level>& f, WidgetNode& node) {
        const Width w{std::max(200.f, node_allocated(node).w.raw())};
```

Next: `examples/widget_gallery/knob.hpp` is a full dial in one header; the task runner's rows
(`examples/model_task_runner/blocks/console_rows.hpp`) are the same idea, read-only.

## 05. Blocks composed

A block is a struct with fields and a `view()`: it lays itself out and knows its own reactions. An
app is blocks composed — `vb.block(...)` drops one in wherever the parent puts it, and the parent
never names a field inside it. This is how the big examples stay readable as they grow.

```cpp
struct Voice {
    Oscillator osc;
    Filter filter;

    void view(app::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.block(osc);
            vb.gap(16.f);
            vb.block(filter);
        });
    }
};
```

Next: `examples/model_synth_rack/blocks/` — six blocks, wired together by one settings `Link`.
