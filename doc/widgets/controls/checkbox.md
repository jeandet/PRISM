# Checkbox

A checkbox holds a yes or no.
A plain `bool` field is a checkbox; `ui::Checkbox` adds a label beside the box.

```cpp
struct Options {
    core::Field<bool> autosave{true};
    core::Field<ui::Checkbox> remember{{.checked = true, .label = "Remember me"}};

    void view(app::ViewBuilder& vb) { vb.vstack(autosave, remember); }
};
```

The [button page](button.md) shows a checkbox in its screenshot.

## `ui::Checkbox`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `checked` | `bool` | `false` | the tick |
| `label` | `std::string` | `""` | the text beside the box; a click on it ticks too |

`value(remember)` reads `checked`; `set_value(remember, false)` clears it and keeps the label.
For a `Field<bool>`, write `autosave = false`.

## Using it

- A click on the box or its label flips it.
- Space or Enter flips the focused checkbox.
- Each flip notifies.

A checkbox field also works as the flag of `.enabled()` and `vb.enabled_if()`: an
"Advanced options" box can grey out the group below it.
See [Greying out](../common.md).

## See also

- [Button](button.md) with `.checked`: a toggle drawn as a pressed button.
- [Choices](choices.md), for more than two states.
