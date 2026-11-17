# Reactive core

| Type | Holds | Widget | Thread rule | Use it for |
|---|---|---|---|---|
| `Field<T>` | a value | yes | logic thread | anything the user sees or edits |
| `State<T>` | a value | no | logic thread | observable state with no UI (tokens, counters) |
| `Derived<T>` | a computed value | read-only | logic thread | values computed from other observables |
| `List<T>` | a collection | via table/tree | logic thread | rows, with insert/erase notifications |
| `Shared<T>` | the latest value | via inspector | **any thread** | state published by workers; old values are dropped |
| `Channel<T>` | every value, in order | no | **any thread** | event streams where nothing may be lost |

```cpp
core::Derived<double> total{[&]{ return price.get() * quantity.get(); }};

auto conn = price.on_change().connect([](const double& p) { /* ... */ });   // RAII handle

prism::transaction([&] {   // one notification at the end, not one per set()
    price.set(9.99);
    quantity.set(3);
});
```

A few rules worth knowing:

- `set()` with an equal value does not notify.
- `value(x)` reads what a field holds, unwrapped from its widget: a slider's number, a text
  field's text, a checkbox's tick, a toggle button's on/off, or the plain value of a `Field<int>`.
  It reads through `get()`, so a `Derived` follows it. A local named `value` hides it: name
  parameters something else.
- `set_value(x, v)` changes that same member and keeps the others: a slider keeps its range and
  label, a text field its placeholder. `x.update([](auto& w) { ... })` changes several members in
  one notification. `x.set({...})` builds a whole new widget: every member it does not name goes
  back to its default. A `Derived` has neither, and `set_value(name, 65)` does not compile.
- `show(dialog)` opens a dialog with no answer yet; `show(popup)` opens a popup as it was made.
- `Field<T>` holds only a value, never a label. The label comes from the member name.
- One logic thread owns the widget tree and every `Field<T>`. They are not thread-safe.
- Other threads talk to it through `Shared<T>::set()`, `Channel<T>::send()` or a posted closure.
- A `Shared<T>::set()` or `Channel<T>::send()` wakes an idle app. `ctx.receive(x, callback)` (in
  the setup, or in a block's `attach()`) hands what arrives to the callback on the logic thread;
  members the widget tree shows (`vb.widget(shared)`) are wired on their own.
- An exception in a callback goes to `core::set_unhandled_error_handler` and never stops the loop.
- `transaction()` coalesces notifications. It does not roll back on exception.
- A `Derived` follows the observables its function reads through `get()`, again at every
  recompute: a branch not taken is not followed until it is. `core::untracked(fn)` reads without
  following. A list after the function (`{fn, price, quantity}`) follows exactly those instead.

## Connecting blocks

Like Qt's `connect(sender, signal, context, lambda)`, a connection lasts as long as the block that
reacts, with no base class: a `core::Wiring` member holds it. Either side may die first: when the
watched field goes, its connections go quiet.

```cpp
struct Summary {
    core::Field<ui::Label<>> text{};
    core::Wiring wire;   // its connections die with the block

    explicit Summary(Order& order) {
        wire.on(order.customer, [this](const auto& c) { greet(c); });
        wire.on_any([this] { recompute(); }, order.price, order.quantity, order.discount);
    }
};
```

One block's output can feed another's input: `wire.link(mixer.level, gain)` copies the value now
and after each change, and `wire.link(percent, label, fn)` converts it on the way. Two links in
opposite directions settle, because setting an equal value emits nothing.

A block reacting to its own fields can call `field.observe(fn)`: both die together. A value
computed from other fields wants a `Derived`, which needs no wiring at all.
