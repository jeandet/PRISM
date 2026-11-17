# Reference

One page per type or facility, with its whole public API.
For how they fit together, read [The model](../model.md), [Views and layout](../views.md),
[Blocks](../blocks.md) and [Threads](../threads.md) first.

| Page | Covers |
|---|---|
| [Field and State](field.md) | a value that notifies; `=`, `+=`, `++`; `value`, `set_value`, `update` |
| [Derived](derived.md) | a value computed from others |
| [List](list.md) | rows that tell their views what changed |
| [Shared](shared.md) | the newest value, from any thread |
| [Channel](channel.md) | every value, in order, from any thread |
| [Latest](latest.md) | the newest value, from a real-time thread |
| [Signals and connections](signals.md) | `Wiring`, `Connection`, `SenderHub`, `then`, `on` |
| [Transactions and untracked reads](transaction.md) | `prism::transaction`, `core::untracked`, the error handler |
| [Annotations](annotations.md) | `range`, `unit`, `label`, `tooltip`, `skip`, `readonly`, `section` |
| [ViewBuilder](view-builder.md) | every layout call, and the handles they return |
| [app::run and AppContext](app.md) | starting an app, its options, and what the running app offers |

```{toctree}
:hidden:

field
derived
list
shared
channel
latest
signals
transaction
annotations
view-builder
app
```
