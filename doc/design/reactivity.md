# Reactivity — State Management & Event Dispatch

> **Status: Superseded** by [field/sender/widget spec](../../docs/superpowers/specs/2026-03-27-field-sender-widget-design.md). The architecture has evolved from MVU to **Model-View-Behavior (MVB)**. See [README.md](README.md) for the current architecture description.

## Overview (Historical)

This document originally described an MVU (Model-View-Update) approach. PRISM now uses **MVB** — Model (`Field<T>` structs), View (`Widget<T>` rendering, automatic via reflection), Behavior (user-written `on_change()` / `observe()` chains). The observer/sender pattern described below evolved into `SenderHub` + `core::then` / `core::on` pipe composition with stdexec schedulers.

## User-Facing API: .on_X() Handlers

```cpp
ui.button("Save").on_click([](auto& s) {
    s.saved = true;
});

ui.text_field(ui->name).on_change([](auto& s, auto val) {
    s.name = std::move(val);
});

ui.checkbox(ui->agree).on_toggle([](auto& s) {
    s.agree = !s.agree;
});
```

Handlers take `State&` and mutate it. They are stored in the snapshot's event table and dispatched by the framework when hits are resolved. The view lambda is then re-run with the new state.

## State Flow

```
         ┌──────────────────────────────────┐
         │           App Thread             │
         │                                  │
         │  State ──→ view(ui) ──→ Snapshot  │
         │    ▲                       │      │
         │    │                       │      │
         │    └── .on_X() handler ◄───┘      │
         │         (mutates State)    hit    │
         └──────────────────────────────────┘
                                      │
                              atomic swap
                                      │
         ┌──────────────────────────────────┐
         │         Backend Thread            │
         │  Snapshot ──→ rasterise ──→ present│
         │  InputEvent ──→ hit resolve ──→ ↑ │
         └──────────────────────────────────┘
```

## Internal: Signals (Framework Use)

Signals are used internally for framework-level notifications (window events, timer ticks, animation frames). They are not part of the user API.

```cpp
template <typename... Args>
class signal {
    std::vector<std::function<void(Args...)>> receivers_;
public:
    connection connect(std::function<void(Args...)> fn);
    void emit(Args... args) const;
};
```

## Internal: Change Detection

The framework needs to know when state has changed to decide whether to re-run the view. Options:

1. **Always re-run** after any `.on_X()` handler (simplest, sufficient for most apps)
2. **Dirty flag** set by handlers (opt-in optimisation)
3. **Snapshot comparison** — diff old and new state (expensive for large states)
4. **Observable fields** with change signals (fine-grained, future optimisation with reflection)

For Phase 2/3: always re-run. Optimise later if profiling shows it matters.

A field set while a frame is built (a node board's fit, made in its layout) gets the next
frame: its widgets stay dirty after the publish, and the app publishes again at once.

## Future: Reflection-Derived UI

With C++26 static reflection, the framework can auto-derive UI from state types:

```cpp
struct TaskEditor {
    Field<std::string> title{.label = "Title", .placeholder = "Enter task..."};
    Field<bool> completed{.label = "Done"};
    Field<Priority> priority{.label = "Priority"};
};

auto view = form_view<TaskEditor>(state.editor);
```

`Field<bool>` → checkbox. `Field<std::string>` → text field. `Field<Enum>` → dropdown. No registration, no macros — the type is the specification. This is the Pydantic parallel.

## Future: Observable Collections

For large lists (virtual scrolling, tables), full re-render on every change is too expensive. Observable collections publish diffs:

```cpp
template <typename T>
class observable_collection {
    std::vector<T> items_;
    signal<CollectionDiff<T>> changed_;
public:
    void append(T item);
    void remove(size_t i);
    void update(size_t i, T item);
};
```

The view consumes diffs incrementally. This is a Phase 4+ optimisation.

## Thread Safety

All state mutation happens on the app thread. The view lambda runs on the app thread. `.on_X()` handlers run on the app thread. The only cross-thread data is the immutable `SceneSnapshot`.

## Derived Follows What It Reads

`core::Derived<T>` used to recompute only from a source list written after its function. A source
left out was invisible to the compiler and to a test: the value went stale (the synth stayed
silent once because the power button was missing from the list). Now the list is optional.

Without one, the `Derived` records what its function reads (`core/read_log.hpp`): `Field`/`State`,
`Shared` and `Derived` note every `get()` into a thread-local log while a compute runs. Each new
value read is connected at the moment of the read, while it is surely alive; a value followed by
the previous compute hands its connection over instead of connecting again, and one no longer
read is disconnected. Reads are recorded at every recompute, so the `Derived` follows the branch
its function took, not the one it did not. `core::untracked(fn)` reads without following. A
`Derived` built inside another one's compute saves and restores the log, so its reads stay its
own.

Costs: every `get()` asks libprism's thread-local "is a compute recording?" in release builds too
(a function call: an inline variable would have one copy per shared object, see AGENTS.md). Release
benchmarks did not move (50k widgets, unchanged publish: 1.34 ms). A value read some other way
than `get()` (a raw pointer, a copy taken earlier) is not seen.

With a list (`{fn, a, b}`), it follows exactly those, as before, and a debug build warns once when
the first compute reads a value the list does not cover.

Not solved, as before: a diamond (C reads A and B, B reads A) may recompute C with a stale B, then
again. Ordering recomputes by depth would fix it.

## Open Questions

- Should the framework support async state updates (e.g., HTTP response arrives, needs to update state)?
- Undo/redo: snapshot the entire state before each `.on_X()` handler? Or require the user to implement it?
- Computed/derived state: should there be a `computed([](const State& s) { ... })` mechanism for expensive derivations?
