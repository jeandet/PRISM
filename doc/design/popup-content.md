# Popups that hold widgets

**Status:** implemented 2026-11-02 (plan item P1 #5 in `doc/review-2026-11-02.md`): `ui::Popup`,
`ViewBuilder::popup`, `WidgetTree::layout_popup`/`flatten_popup`/`focus_ring`, routing in
`app/event_routing.hpp`; `model_notes` uses it for File > Rename…. Builds on
`doc/design/menus-and-dialogs.md`. Revised after an opencode challenge: a second layout pass and
drawing in the normal geometry were dropped. Hit testing checks the overlay first (scrollbar
thumbs live there), and a second pass would fight the per-widget draw cache.

## Problem

A popup today is one widget (the "owner": `Menu`, `ContextMenu`, `Dialog`) that paints itself
into the snapshot overlay and gets all input while open. That is right for menus. It can't give
a dialog real fields (a settings form, a name to type, a list to pick from), or a popover
attached to a widget. Every such popup would have to re-implement text editing, focus,
scrolling and accessibility inside its own painter.

## Decision

A popup can **host a subtree**: ordinary widgets, laid out and drawn by the tree like any
other, shown on top of the window while the popup is open.

```cpp
struct Settings {
    Field<ui::Popup> prefs{{.modal = true}};          // open / close, like Dialog::open
    Field<ui::TextField<>> name{{}};
    Field<ui::Checkbox> dark{{.checked = false, .label = "Dark theme"}};
    Field<ui::Button> ok{{"OK"}};
    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.popup(prefs, [&](app::WidgetTree::ViewBuilder& p) { p.vstack(name, dark, ok); });
    }
};
```

- **Built on open, dropped on close.** The subtree exists only while the popup is open. It is
  built from a stored builder when the popup opens, the way a tab's content is built when its
  tab is selected (`WidgetTree::materialize_tabs`), and dropped when it closes. While closed,
  its widgets aren't indexed, focusable, dirty or laid out, so Tab can't reach them.
- **Layout: the same pass, at the popup's rect.** The popup is a zero-size anchor in the flow,
  as `ContextMenu` and `Dialog` are today. During layout its content is measured, given a rect
  (a modal one centred in the window, a non-modal one placed by `place_popup` at the anchor,
  both capped to the window), and arranged there. There's no second pass, so viewports
  (scroll areas, lists, tables) are known in the frame that shows them, and the draw caches
  see a stable rect while it stays open.
- **Drawing and hit testing: the overlay.** The content is flattened into `snap.overlay` and
  `snap.overlay_geometry`, as menus are. The overlay draws after everything and is hit-tested
  first. The content's entries come after the main tree's scrollbar thumbs, so they win. They
  are clipped to the card, so a scroll area inside works. A modal popup draws a scrim first.
- **Input.**
  - Inside the card: ordinary routing (hit test, focus, keys to the focused widget) for the
    content's widgets.
  - Outside: a non-modal popup closes. For a modal one the press is swallowed (today's rule).
  - Escape closes the popup. A widget that wants Escape inside a popup is not supported yet,
    because delegates don't report whether they used a key.
- **Focus.** Opening focuses the first focusable widget in the content. Tab and Shift+Tab
  cycle inside it while it is open. Closing gives focus back to what had it before.
- **Scrolling.** The wheel scrolls scroll views inside the content.
- **Accessibility.** The content's nodes are ordinary widgets. They get real bounds once the
  access tree has bounds (`app/access_tree.hpp`, `simplify:`).

`Dialog` keeps its self-painted form for the simple "message and buttons" case. A dialog with
fields is a `Popup` whose subtree has the fields and a row of `Button`s.

## Not in this step

- **Several popups open at once.** A dropdown or menu *inside* a popup is the first case. The
  tree tracks one popup owner, so opening one would close the other. Stacking needs a list of
  owners; add it with the editable combo box, which needs it.
- **A popup sized to its anchor** (a combo box's list as wide as the box). It comes with the
  editable combo box (P1 #7).
- **Animations** for opening and closing.

## Tests to write first

1. A field inside an open popup gets typed text; the field under the popup does not.
2. Tab cycles inside a modal popup and never reaches the widgets behind it.
3. A press outside a modal popup reaches nothing; outside a non-modal one, it closes it.
4. A popover near the right or bottom edge flips (`place_popup`).
5. Closing gives focus back.
6. The popup's widgets are on top: a press over a scrollbar of the window behind the card
   reaches the card's widget, not the scrollbar.
7. A closed popup's widgets are not reachable by Tab.
