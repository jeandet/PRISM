# Signals and connections

`#include <prism/prism.hpp>` · namespace `prism::core`

Every observable value has a signal: `on_change()` on a field, `on_insert()` on a list, and so on.
A signal calls its connected functions when it fires. Connecting returns a `Connection`, and the
function stays connected as long as that `Connection` lives.

Most code never touches these directly. `observe(fn)` covers a struct reacting to its own fields,
and a `Wiring` member covers reactions to other structs. This page documents all three layers.

| Need | Use |
|---|---|
| react to my own field | `field.observe(fn)` |
| react to another block's field, until my block goes | `wire.on(field, fn)` |
| copy a value from one block to another | `wire.link(source, target)` |
| a reaction I end myself | `auto c = field.on_change().connect(fn)` |
| a signal of my own | a `SenderHub<Args...>` member |

## Wiring

`Wiring` keeps the connections a block makes to other blocks, and ends them when the block ends.
It is Qt's "context object", as a member instead of a base class.

```cpp
struct Status {
    core::Field<ui::Label<>> text{};
    core::Field<bool> unsaved{false};
    core::Wiring wire;   // last: destroyed first, before the fields it writes to

    Status(Editor& editor, Theme& theme) {
        wire.on(editor.path, [this](const std::string& p) { set_value(text, p); });
        wire.on_any([this] { refresh(); }, theme.dark, theme.accent);
        wire.link(editor.modified, unsaved);   // unsaved follows editor.modified
    }

    void refresh();
};
```

| Member | Meaning |
|---|---|
| `on(source, fn)` | calls `fn(value)` after each change of `source`: a `Field`, `State`, `Derived`, `Shared`, or anything with `on_change()` |
| `on(hub, fn)` | the same for a bare signal (`SenderHub`), such as `list.on_insert()` |
| `on_any(fn, a, b, ...)` | calls `fn()` after a change of any of the sources |
| `link(source, target)` | sets `target` to `source`'s value now and after each change |
| `link(source, target, convert)` | the same through `convert(value)`, which may change the type |
| `keep(connection)` | keeps a `Connection` made some other way |
| `clear()` | disconnects everything now |
| `size()` | how many connections it holds |

A `Wiring` can be moved, not copied.

Rules:

- **Either side may die first.** A source destroyed first leaves its connections quiet.
- **The target of a `link` must outlive the `Wiring`.** Keep the `Wiring` in the target's block.
- **Two links in opposite directions settle**, because setting an equal value notifies nobody.

## Connection

A `Connection` is the handle `connect` returns. Destroying it disconnects.

| Member | Meaning |
|---|---|
| `Connection()` | an empty handle, connected to nothing |
| move construction, move assignment | the handle moves; assigning disconnects the old one first |
| `disconnect()` | disconnects now; calling it twice is harmless |
| destructor | disconnects |

It cannot be copied. A `Connection` may outlive its signal: it then disconnects from nothing.

```cpp
core::Field<ui::Slider<>> volume{{.value = 0.5}};
core::Connection c = volume.on_change().connect([](const ui::Slider<>& s) { apply(s.value); });
// ... later
c.disconnect();
```

`connect` is `[[nodiscard]]`: a discarded `Connection` disconnects at once.

## SenderHub

`SenderHub<Args...>` is a signal. Every `on_change()` returns one. A struct can declare its own:

```cpp
struct Player {
    core::SenderHub<int> track_finished;   // a signal carrying the track number
    void finish(int n) { track_finished.emit(n); }
};

Player player;
auto c = player.track_finished.connect([](int n) { std::println("track {} done", n); });
player.finish(3);   // prints "track 3 done"
```

| Member | Meaning |
|---|---|
| `connect(fn)` | connects `fn(Args...)`; returns a `Connection` |
| `emit(args...)` | calls every connected function, in the order connected, on the calling thread |
| move construction, move assignment | the connections follow the hub |

It cannot be copied. Connecting and emitting are thread-safe, but the functions run on the thread
that emits: a hub emitted from a worker runs its functions on the worker.

## Pipes: `then` and `on`

A signal can be piped into a function, optionally on a scheduler:

```cpp
core::Field<int> field{0};   // and ctx: the AppContext, in attach or setup
auto c1 = field.on_change() | core::then([](const int& v) { use(v); });

// run the reaction later, as a separate task on the logic thread's loop
auto c2 = field.on_change() | core::on(ctx.scheduler()) | core::then([](const int& v) { use(v); });
```

`hub | core::then(fn)` is `hub.connect(fn)`. With `core::on(scheduler)`, each emission schedules
`fn` on that scheduler instead of calling it at once. `ctx.scheduler()` is the logic thread's
`std::execution` scheduler.
