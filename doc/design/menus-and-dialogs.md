# Menus, popups and dialogs

**Status:** implemented (plan item P2 #12 in `doc/review-2026-11-01.md`): the popup layer
(`WidgetNode::popup_area`/`popup_modal`, `WidgetTree::popup_owner`/`close_popup`/`adopt_popup`,
routing in `app/event_routing.hpp`), `Menu` with menu bar navigation, `ContextMenu`
(`delegates/menu_delegates.hpp`) and a modal `Dialog` with a scrim (`delegates/dialog_delegates.hpp`).
Since 2026-11-02 a dialog is a popup holding widgets (`doc/design/popup-content.md`):
`ViewBuilder::dialog` builds its title (a heading `Label`), its message (a read-only `TextArea`,
which wraps, scrolls and can be selected and copied), the app's own widgets (a form, through
`vb.dialog(d, content)`) and a row of `Button`s, the first one focused so Enter picks it.
Tooltips (`ui/tooltip.hpp`) are drawn in the snapshot overlay and never take input; their
delay is the one timed wake of the logic thread (`app/deadline_timer.hpp`).
Popups open below what they came from (a menu title, a right click) and flip above it, or
right-align with it, near a window edge. Menus have keyboard mnemonics (`"&File"`, Alt+F).
Not yet: popovers anchored to arbitrary widgets.

## Decision

For v1, menus, context menus, popovers, tooltips and modal dialogs are drawn **inside the
window**, on a popup layer of the widget tree. Real OS windows (`WindowRegistry`) stay for
genuinely independent windows (the debug inspector, detachable panels). Native pickers (files,
colours) belong behind a platform service, not in the widget layer.

## Why

- **Testable headless.** Snapshots, golden tests and README captures see an in-window menu; an OS
  popup window is invisible to all of them.
- **No platform positioning rules.** Wayland `xdg_popup` grabs and placement, monitor-edge
  flipping, and cross-window dismissal (a click in the parent must close the popup) never enter
  the code.
- **Where toolkits went.** GTK 4 popovers and menus are drawn in the parent surface; Dear ImGui
  and Flutter draw everything, modals included, in-window. Qt uses popup windows and paid for it
  in platform-specific bugs. Desktop toolkits still use separate windows for modal dialogs; PRISM
  can add that later if needed.

## What exists

- `SceneSnapshot::overlay` + `overlay_geometry`, filled from each node's `overlay_draws`
  (`ui/layout.hpp`, `flatten_overlays`); hit testing checks overlay rects first
  (`input/hit_test.hpp`).
- `close_overlays()` on a press elsewhere (`app/event_routing.hpp`). The dropdown is the only
  popup today, and it is entirely one widget's business (draws into `overlay_draws`, flips up
  near the bottom edge).
- Right-click and click counts arrive on `MouseButton` (`mods`, `clicks`).

## What is missing — two primitives

1. **A tree-level popup layer that swallows the dismissal click.** Today a press outside the
   dropdown closes it *and* activates what is underneath. A menu must consume that click. A
   `WidgetTree` popup slot (owner id, rect) checked first in mouse and key routing: outside →
   close and stop; Escape/Tab/arrows → the owner; no hover underneath while open.
2. **Modality.** A modal depth: while > 0, all input goes to the top modal node; hover, scroll
   and focus changes elsewhere are suppressed; a translucent scrim is drawn behind it.

On top of these: a menu bar is a row of buttons opening popups; context menus open on a right
click at the pointer, clamped and flipped at the window edges (generalizing the dropdown's
flip); a modal dialog is a centered card over the scrim.

## Tests to write first

- A press outside an open popup closes it and does **not** reach the widget underneath.
- Escape closes; Tab and arrows stay inside the popup.
- While modal, clicks and keys outside the dialog reach nothing; the dialog gets focus.
- A context menu opened near the right/bottom edge is placed inside the window.
- Golden snapshot of an open menu.
