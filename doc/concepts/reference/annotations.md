# Annotations

`#include <prism/prism.hpp>` · namespace `prism::core` · needs C++26 reflection

Annotations are C++26 attributes on struct members: `[[=core::tooltip<"...">]]`.
PRISM reads them when it builds a window from a struct, and when it checks a view.
They tune what reflection does, with no `view()` to write.

## Example

```cpp
struct PowerSupply {
    [[=core::range<0.0, 30.0, 0.1>, =core::unit<"V">]] core::Field<double> volts{5.0};
    [[=core::tooltip<"Current limit">]] core::Field<ui::Slider<>> amps{{.value = 1.0, .max = 3.0}};
    [[=core::skip]] core::Field<int> calibration_step{0};
};
```

`volts` becomes a slider from 0 to 30 V, in steps of 0.1, captioned "volts".

## The annotations

| Annotation | On | In a window built by reflection | Elsewhere |
|---|---|---|---|
| `range<min, max, step>` | a number field (`Field<double>`, `Field<int>`...) | a slider over that range; step 0 is continuous | the inspector shows a slider |
| `unit<"V">` | with `range` | printed after the slider's value | the inspector |
| `label<"Sample rate">` | a member | the slider's caption, instead of the member's name | a table's column header; the inspector's caption |
| `tooltip<"...">` | a `Field`, `Derived` or `Shared` | the widget's tooltip | the same when a `view()` places it |
| `skip` | a member | — | the forgotten-field check ignores it; a table leaves the column out; the inspector hides it |
| `readonly` | a member | — | the inspector shows it, not editable |
| `section<"Audio">` | a member | — | the inspector shows a header above it |

The inspector is the [live object inspector](../../app/inspector.md), which shows any struct.

## Rules

- **Reflection only.** Clang and the browser build do not parse `[[=...]]`. Code that must build
  there cannot use annotations: give the slider its range in the field itself
  (`Field<ui::Slider<>> volts{{.min = 0, .max = 30}}`), and keep values that are not widgets in a
  small nested struct.
- **`range` is for numbers.** On another type it is a compile error.
- **A `view()` that places the field with `vb.widget` does not read `range`.** It sees the field,
  not the member. Annotate, or place a `Slider` field.
- **`skip` does not hide a field from a window built by reflection.** It only silences the
  forgotten-field check, and hides a column or an inspector row.
