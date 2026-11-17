# Transactions and untracked reads

`#include <prism/prism.hpp>`

Two functions change how notifications are sent: `prism::transaction` delays them, and
`core::untracked` hides reads from a `Derived`.

## transaction

`prism::transaction(fn)` runs `fn`, and holds every notification until it returns.
Then each changed value notifies once, with its final value.

```cpp
struct Order {
    core::Field<double> price{1.0};
    core::Field<int> quantity{1};
    core::Derived<double> total{[this] { return price.get() * quantity.get(); }};
};

Order order;
order.total.observe([](double t) { std::println("total {}", t); });

prism::transaction([&] {
    order.price = 2.0;
    order.quantity = 5;
});
// prints "total 10" once, not "total 5" then "total 10"
```

| Signature | Meaning |
|---|---|
| `template <class F> void prism::transaction(F&& fn)` | runs `fn()`; notifications wait for the end |

How the end works:

1. Every value changed inside `fn` notifies once, with its final value. They notify in the order
   of their last change.
2. Reactions to those notifications may change more values. They are held too, and sent in a
   next round, again once each.
3. Rounds repeat until nothing changes.

Transactions nest: only the outermost one sends.

Use one when a reaction should see a consistent state: a price and a quantity changed together,
a row of a form filled from a record.

Rules:

- **No rollback.** If `fn` throws, the changes made before the throw stay, and their notifications
  are sent.
- **Logic thread only**, like the values it changes.
- **A cycle stops after 64 rounds.** Two reactions that keep changing each other are a bug; a debug
  build asserts.

## untracked

A `Derived` follows every value its function reads. `core::untracked(fn)` runs `fn` without
recording its reads, and returns what `fn` returns:

```cpp
struct Meter {
    core::Field<double> level{0.0};
    core::Field<std::string> unit{"dB"};
    core::Derived<std::string> text{[this] {
        auto u = core::untracked([&] { return unit.get(); });
        return std::format("{:.1f} {}", level.get(), u);
    }};
};
```

`text` recomputes when `level` changes. A change of `unit` alone does not recompute it; the next
change of `level` picks up the new unit.

| Signature | Meaning |
|---|---|
| `template <class F> decltype(auto) core::untracked(F&& fn)` | runs `fn()` with no read recorded; returns its result |

## Unhandled errors

An exception that escapes a reaction or a posted function goes to the unhandled-error handler,
and the app goes on.

| Signature | Meaning |
|---|---|
| `void core::set_unhandled_error_handler(std::function<void(std::exception_ptr)> h)` | replaces the handler; any thread; `nullptr` restores the default, which prints to stderr |
