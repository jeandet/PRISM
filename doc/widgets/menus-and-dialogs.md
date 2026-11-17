# Menus and dialogs

Popups draw above the window: menus, dialogs, popovers and tooltips.
They open on an in-window layer that takes input first: a click outside a menu closes it
without reaching the widget underneath. Popups always stay inside the window: near an edge,
a menu opens above its title.

| Page | For | Field type |
|---|---|---|
| [Menu](popups/menu.md) | a menu bar's menus, with shortcuts and submenus | `ui::Menu` |
| [Context menu](popups/context-menu.md) | a menu on right-click | `ui::ContextMenu` |
| [Dialog](popups/dialog.md) | a modal question, or a small form | `ui::Dialog` |
| [Popover](popups/popup.md) | widgets in a popup by a button | `ui::Popup` |
| [Tooltip](popups/tooltip.md) | a hint when the pointer rests | a handle method |
| [File dialogs and message boxes](popups/file-dialogs.md) | the platform's own dialogs, and files dropped on the window | `app::AppContext` calls |

```{toctree}
:maxdepth: 1
:hidden:

popups/menu
popups/context-menu
popups/dialog
popups/popup
popups/tooltip
popups/file-dialogs
```
