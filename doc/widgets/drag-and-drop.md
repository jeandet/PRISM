# Drag and drop

A widget can be dragged, and another can take what is dropped on it.
The value dragged is typed: a target takes the type its function names, and refuses others
with a no-entry cursor.

```cpp
struct Track { int id; };

struct Player {
    core::Field<ui::Label<>> song{{"Blue in Green"}};
    core::Field<ui::Label<>> playlist{{"Drop songs here"}};
    std::vector<int> queue;

    void view(app::ViewBuilder& vb) {
        vb.widget(song).drag_source([] { return Track{42}; });
        vb.widget(playlist).drop_target([this](const Track& t, ui::DropPlace) {
            queue.push_back(t.id);
        });
    }
};
```

While dragging, the source follows the pointer, faded, and a target that takes the value is
outlined. Escape cancels.

## The handle methods

| Method | Does |
|---|---|
| `.drag_source(make)` | pressing the widget and moving drags the value `make()` returns, of any type; a `std::optional` left empty drags nothing this time |
| `.drop_target(on_drop)` | `on_drop(const T& value, ui::DropPlace at)` takes dropped values of type `T` |

`ui::DropPlace` says where the drop landed, in the target's coordinates:

| Member | Type | Meaning |
|---|---|---|
| `at` | `core::Point` | where the pointer was |
| `size` | `core::Size` | the target's size |
| `slot` | `std::size_t` | for targets with items (a tab bar), the gap between which items |

A dragged `std::string` dropped on a text field is typed into it, with no code.

## Built-in drags

| Widget | Drag | Turn it on |
|---|---|---|
| [List](data/list.md) | rows, to reorder them | `.reorderable()` |
| [Tree](data/tree.md) | nodes, into or beside others | set `controller.on_move` |
| [Tabs over a list](layout/tabs.md) | tabs, to reorder them | `.reorderable()` |
| [Table](data/table.md) | headers, to reorder columns | always |
| [Dock](docking.md) | panels, between groups and out of the window | always |

## Clicks and drags

A click lands on the release, over the widget that was pressed, as on every platform.
A drag that starts on a button is not a click, and sliding off a button before letting go
cancels it.

## See also

- [File dialogs and message boxes](popups/file-dialogs.md): files dropped from outside the app.
