# Color

A color field shows a swatch and its hex code. A click opens a picker:
a square for saturation and brightness, and a bar for the hue.

```cpp
struct Pen {
    core::Field<core::Color> ink{core::Color::rgba(0x33, 0x66, 0xCC)};

    void view(app::ViewBuilder& vb) { vb.widget(ink); }
};
```

The [choices page](choices.md) shows a color field, closed, in its screenshot.

## `core::Color`

| Member | Type | Meaning |
|---|---|---|
| `r`, `g`, `b` | `std::uint8_t` | red, green and blue, 0 to 255 |
| `a` | `std::uint8_t` | opacity, 0 (clear) to 255 (solid) |
| `Color::rgba(r, g, b, a = 255)` | | builds one |

The picker sets red, green and blue, and keeps the opacity the field had.

## Using it

- A click, Space or Enter opens the picker. Enter or Escape closes it.
- In the picker, drag in the square or the hue bar.
- Left and Right nudge the saturation, Up and Down the brightness.
- Every change notifies, at each move of a drag.

## See also

- [Theme colors](../../app/windows.md): the colors widgets draw with.
