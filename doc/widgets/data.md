# Data views

Data views show collections: a list of items, a table of rows and columns, a tree of nodes, and a
board of cards joined by wires. A list, a table and a tree build only what is on screen, so a
million rows scroll as fast as ten.

| Widget | Shows | Bound to |
|---|---|---|
| [List](data/list.md) | one row per item | a `core::List<T>` |
| [Table](data/table.md) | rows and columns, sortable and editable | your own source, or a `core::List<Struct>` |
| [Tree](data/tree.md) | nodes that open and close | your own source, or a struct |
| [Node board](data/node-board.md) | cards joined by wires, edited in place | a `nodes::Graph` |

A table and a tree read your data where it already is, through a small interface you
implement, or through reflection over a struct. Nothing is copied into a model class.

```{toctree}
:maxdepth: 1
:hidden:

data/list
data/table
data/tree
data/node-board
```
