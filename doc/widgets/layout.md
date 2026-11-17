# Layout

A `view()` method arranges widgets with containers. Containers nest: a row inside a column
inside a tab. There are no layout objects to keep: the calls describe the arrangement, and
PRISM keeps the widgets it builds from them.

| Page | Containers |
|---|---|
| [Rows, columns and splits](layout/stacks.md) | `vstack`, `hstack`, `spacer`, `gap`, `handle`, `remember_split` |
| [Groups, sections and bars](layout/frames.md) | `group`, `section`, `toolbar`, `status_bar` |
| [Scroll areas](layout/scroll.md) | `scroll` |
| [Tabs and pages](layout/tabs.md) | `tabs`, `tab`, `pages`, tabs over a list |
| [Docking](docking.md) | `dock`: panels the user rearranges |

A nested struct with a `view()` of its own is placed with `vb.block(child)`:
see [blocks](../concepts/blocks.md).

```{toctree}
:maxdepth: 1
:hidden:

layout/stacks
layout/frames
layout/scroll
layout/tabs
```
