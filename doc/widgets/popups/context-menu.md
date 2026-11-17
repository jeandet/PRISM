# Context menu

A context menu opens on a right-click, where the pointer is.
It belongs to the container it is declared in: a right-click anywhere in that container opens it.

```cpp
struct Page {
    core::Field<ui::TextArea<>> text{{.value = "Right-click me.", .rows = 4}};
    core::Field<ui::ContextMenu> actions{{.items = {{"Cut"}, {"Copy"}, {"Paste"}}}};
    core::Connection on_action = ui::on_choice(actions, [](std::size_t i) { /* 0: Cut ... */ });

    void view(app::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(text);
            vb.context_menu(actions);   // a right-click in this column opens it
        });
    }
};
```

A context menu takes no space in the layout.
Near an edge, it opens up or left of the pointer so it stays inside the window.

## `ui::ContextMenu`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `items` | `std::vector<ui::MenuItem>` | none | the items, as in a [menu](menu.md) |
| `chosen` | `std::optional<std::size_t>` | none | the last top-level item chosen |
| `chosen_path` | `std::vector<std::size_t>` | none | the path to it, through submenus |
| `activations` | `std::uint64_t` | `0` | counts the choices |

Items have the same members as a [menu's](menu.md): shortcuts, icons, check marks,
greyed-out items, separators and submenus.
`ui::on_choice` hears the choices, as for a menu.

## Using it

The keys in an open context menu are a menu's: Up, Down, Enter, Escape, Right and Left for
submenus.

## See also

- [Menu](menu.md).
- [Table](../data/table.md): the gallery hides columns with a context menu on the header.
