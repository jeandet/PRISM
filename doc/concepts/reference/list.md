# List

`#include <prism/prism.hpp>` · namespace `prism::core`

`List<T>` is a vector of rows that tells its views what changed.
A list, table or tabs placed over it update only the rows that changed, and draw only the rows on
screen: 100,000 rows cost what is visible.

## Example

```cpp
struct Task {
    std::string name;
    bool done = false;
    bool operator==(const Task&) const = default;
};

struct Todo {
    core::List<Task> tasks;
    void view(app::ViewBuilder& vb) { vb.table(tasks); }   // a column per member (reflection)
};

Todo todo;
todo.tasks.push_back({"write docs"});
todo.tasks.push_back({"review"});
todo.tasks.set(0, {"write docs", true});
todo.tasks.move(1, 0);        // "review" first
todo.tasks.erase(1);
```

## Changing it

Each call notifies once. Logic thread only.

| Member | Meaning | Signal |
|---|---|---|
| `push_back(v)` | adds `v` at the end | `on_insert` (index, value) |
| `insert(i, v)` | puts `v` at row `i`; the rows from `i` on move down one (`i == size()` appends) | `on_insert` (index, value) |
| `erase(i)` | removes row `i` | `on_remove` (index) |
| `clear()` | removes every row, last first | `on_remove` for each |
| `set(i, v)` | replaces row `i` | `on_update` (index, value) |
| `move(from, to)` | moves a row; `to` is its index after the move | `on_update` for each shifted row, then `on_move` (from, to) |
| `replace_all(range)` | the list becomes `range`: rows past its end removed or added, rows in both updated only if they differ | the signals of those changes |

`replace_all` is the way to refresh a list from a new result: views keep the widgets of unchanged rows.

## Reading it

| Member | Meaning |
|---|---|
| `l[i]` | row `i`, `const T&`: rows are changed through `set` |
| `size()`, `empty()` | |
| `begin()`, `end()` | iterate over the rows, read-only |

## Signals

| Member | Arguments |
|---|---|
| `on_insert()` | `(size_t index, const T& value)` |
| `on_remove()` | `(size_t index)` |
| `on_update()` | `(size_t index, const T& value)` |
| `on_move()` | `(size_t from, size_t to)` |

Each is a `SenderHub`; `connect(fn)` returns a [`Connection`](signals.md).
`core::index_after_move(index, from, to)` says where a row tracked by index went after a move.

## Shown by

| Call | Shows |
|---|---|
| `vb.list(list)` | one row per item, drawn by `T`'s widget |
| `vb.table(list)` | a column per member of `T` (needs reflection) |
| `vb.tabs(selected, list, title, page)` | a tab per item |

See [Widgets](../../widgets/data.md) for each.

## Pitfalls

- **Rows are values.** A row holding `Field`s is not watched field by field: change a row with `set`.
- **`replace_all` compares rows** with `==` when `T` has it; without it, every row in both is updated.
