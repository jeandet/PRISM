# Tooltip

A tooltip is a hint that shows when the pointer rests on a widget.
A click, a key or a scroll hides it. Long text wraps.

```cpp
struct Toolbar {
    core::Field<ui::Button> save{{"Save"}};

    void view(app::ViewBuilder& vb) {
        vb.widget(save).tooltip("Write the file to disk");
    }
};
```

## Three ways to set one

| Where | How |
|---|---|
| in `view()` | `vb.widget(x).tooltip("...")`; text views and editors take `.tooltip` too |
| on a reflected member | `[[=core::tooltip<"...">]]` before the member |
| for a widget placed by code you do not write | `vb.tooltip_for(field, "...")`, called before it is placed |

```cpp
struct Mixer {
    [[=core::tooltip<"Output level, in dB">]] core::Field<ui::Slider<>> gain;
};
```

A widget with no text of its own, an icon-only button say, should have a tooltip:
assistive technology reads it as the widget's name.

## See also

- [What every widget does](../common.md).
