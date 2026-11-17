# Scroll areas

A scroll area shows content taller than its space, with a scrollbar.

```cpp
struct LongForm {
    std::array<core::Field<ui::TextField<>>, 40> answers{};

    void view(app::ViewBuilder& vb) {
        vb.scroll([&] {
            for (auto& a : answers) vb.widget(a);
        });
    }
};
```

Lists, tables, trees, text areas and document views scroll by themselves.
They need no scroll area.

## The calls

| Call | Does |
|---|---|
| `vb.scroll(fn)` | scrolls what `fn` places; a scrollbar when needed |
| `vb.scroll(policy, fn)` | the same, with a scrollbar policy |
| `vb.scroll(area, fn)` | the same, with the position in a `Field<ui::ScrollArea>` |

`ui::ScrollBarPolicy` is `Auto` (shown when the content overflows), `Always` or `Never`.

## `ui::ScrollArea`

Keep the scroll position in a field to save it, or to scroll from code.

| Member | Type | Default | Meaning |
|---|---|---|---|
| `scrollbar` | `ui::ScrollBarPolicy` | `Auto` | when the scrollbar shows |
| `event_policy` | `ui::ScrollEventPolicy` | `BubbleAtBounds` | what a wheel turn does at either end |
| `scroll_y` | `core::DY` | `0` | how far down it is scrolled |
| `scroll_x` | `core::DX` | `0` | how far right it is scrolled |

With `BubbleAtBounds`, a wheel turn past the top or the bottom scrolls the area around it,
as a web page does. `ConsumeAlways` keeps every turn.

## Using it

- The wheel and the scrollbar scroll.
- Page Up and Page Down scroll the area holding the focused widget.

## See also

- [List](../data/list.md): a long list of rows, built only where they show.
