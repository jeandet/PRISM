# Progress bar

A progress bar shows how far along some work is.
For work of unknown length, it shows that something is happening.

```cpp
struct Export {
    core::Field<ui::ProgressBar> done{{.value = 0.62, .label = "62% exported"}};
    core::Field<ui::ProgressBar> waiting{{.label = "Connecting", .busy = true}};

    void view(app::ViewBuilder& vb) { vb.vstack(done, waiting); }
};
```

The [label page](label.md) shows both bars in its screenshot.

## `ui::ProgressBar`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `value` | `double` | `0` | how far along, between `min` and `max` |
| `min` | `double` | `0` | the empty end |
| `max` | `double` | `1` | the full end |
| `label` | `std::string` | `""` | text drawn over the bar |
| `busy` | `bool` | `false` | work of unknown length: a moving band instead of a fill |

Change one member and keep the others with `update`:

```cpp
done.update([](ui::ProgressBar& p) { p.value = 0.8; p.label = "80% exported"; });
```

Progress usually comes from a worker thread. Send it to the model through a `Shared<double>`
and copy it into the bar on the logic thread: see [threads](../../concepts/threads.md).

## Using it

A progress bar takes no focus and no input.

## See also

- [Slider](slider.md): a number the user changes.
- [Status bar](../layout/frames.md): where a progress bar often sits.
