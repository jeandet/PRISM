# Field and State

`#include <prism/prism.hpp>` · namespace `prism::core`

`Field<T>` holds one value of type `T`, and notifies its listeners when the value changes.
It is the building block of every model: the type `T` decides which widget shows it.
`State<T>` is the same, with no widget: observable state the window never shows.

## Example

```cpp
struct Cart {
    core::Field<int> items{0};
    core::Field<std::string> owner{"ada"};
    core::Field<ui::Slider<>> discount{{.value = 0.0, .min = 0.0, .max = 0.5}};
    core::State<int> version{0};   // observable, never shown
};

Cart cart;
cart.items.observe([](int n) { std::println("{} items", n); });

cart.items += 2;               // prints "2 items"
++cart.items;                  // prints "3 items"
cart.owner += " lovelace";     // "ada lovelace"
set_value(cart.discount, 0.1); // the slider's value; its range stays
if (cart.owner == "ada lovelace") { /* ... */ }
```

## Construction

| Form | Meaning |
|---|---|
| `Field<T> f;` | holds `T{}` |
| `Field<T> f{v};` | holds `v` |
| `Field<ui::Slider<>> f{{.value = 0.5, .max = 2.0}};` | the outer braces are the field's, the inner ones build the widget |

A field cannot be copied: its listeners belong to it.

## Reading

| Member | Returns | Meaning |
|---|---|---|
| `get()` | `const T&` | the value |
| conversion to `const T&` | `const T&` | a field reads as its value: `int n = cart.items;` |
| `value(f)` (free function) | the widget's value | the number in a slider, the text in a text field, the tick of a checkbox; the value itself for a plain `T` |

Reading through `get()`, the conversion or `value()` is what a [`Derived`](derived.md) follows.

## Writing

All of these notify once if the value changed, and not at all if it did not.
Inside a [transaction](transaction.md), the notification waits for its end.

| Member | Meaning |
|---|---|
| `set(v)` | stores `v` |
| `f = v` | the same as `set(v)` |
| `f = {.member = ...}` | builds a new `T` from a designated initializer: members not named go back to their defaults |
| `update(fn)` | calls `fn(T&)` on a copy, then sets it: changes several members in one notification |
| `mutate(fn)` | calls `fn(T&)` on the stored value itself, no copy: a string keeps its memory. Always notifies |
| `set_value(f, v)` (free function) | changes only the widget's value member, keeps the others |
| `f += v`, `-=`, `*=`, `/=`, `%=`, `\|=`, `&=`, `^=` | the operator on the value, then `set`; exists when `T` has it |
| `++f`, `--f` | the same; return the field |
| `f++`, `f--` | the same; return the old value |

```cpp
core::Field<ui::ProgressBar> progress{{.value = 0.0, .label = "Exporting"}};
progress.update([](ui::ProgressBar& p) { p.value = 0.5; p.busy = false; });   // one notification
```

`mutate` is for text or lists changed often, such as a status line updated every second on
a microcontroller. `update` and `set` build a new value, which allocates for a long string;
`mutate` writes into the one the field holds, so nothing is allocated once the text fits. It
notifies every time, since no old value is kept to compare with.

```cpp
core::Field<ui::Label<>> status{{.value = "starting"}};
status.mutate([&](ui::Label<>& l) { l.value.assign(line); });   // reuses the string's memory
```

## Comparing

`f == v`, `f != v`, `f < v` and the other comparisons compare the value with anything that
converts to `T`. They exist when `T` has them.

```cpp
core::Field<std::string> name{"ada"};
bool same = name == "ada";   // true
```

Comparing two fields reads both values: `a.get() == b.get()`.

## Listening

| Member | Meaning |
|---|---|
| `observe(fn)` | calls `fn(const T&)` after each change, for as long as the field lives |
| `on_change()` | the field's signal: `connect(fn)` returns a [`Connection`](signals.md) that ends the reaction when destroyed |

```cpp
cart.items.observe([](int n) { /* runs after each change */ });
auto c = cart.items.on_change().connect([](const int& n) { /* until c is destroyed */ });
```

To react to another struct's fields, use a [`Wiring`](signals.md#wiring).

## Thread rules

A field belongs to the logic thread while the app runs.
Reading or writing it from another thread is a data race; a debug build asserts on a write.
Hand values over with a [`Shared`](shared.md), a [`Channel`](channel.md) or `ctx.post`
(see [Threads](../threads.md)).

## Pitfalls

- **`a = b` between two fields does not compile.** Write `a = b.get()`. Copying a field would copy
  or drop its listeners; copying the value is what you mean.
- **`f = {...}` resets the members you do not name.** To change one member, use `set_value` or `update`.
- **Setting an equal value notifies nobody.** A reaction that must run anyway belongs elsewhere
  (a button's click count changes on every click, so a button always notifies).
- **A `Field` holds a value, never a label.** The label of a reflected window comes from the member
  name, or from a `label` [annotation](annotations.md).
- **Moving a field moves its listeners.** Fields in a `std::vector` that reallocates take their
  connections along. Prefer `List<T>` for rows, or `std::array` / `std::deque` for fields that
  must stay put.

## State

`State<T>` has the same members as `Field<T>`: construction, reading, writing, operators and
listening. The only difference: a window never shows it. Use it for a value that drives behavior
but is not displayed: a session token, a counter of saves, a mode.

```cpp
struct Editor {
    core::Field<ui::TextArea<>> body{{.rows = 12}};
    core::State<int> saves{0};
    core::Derived<std::string> title{[this] { return std::format("Notes ({} saves)", saves.get()); }};
};
```
