# Drag and drop inside the app, and docking

**Status:** proposal, 2026-11-12, not built. Written for review before any code: the choices in
"Open questions" are worth a decision first. Plan items P1 #6 and #7 in
`doc/review-2026-11-12.md`.

## Problem

Files and text can be dropped *on* the app from the OS (`FileDrop`, `TextDrop`). Nothing can be
dragged *within* it: no reordering a list, no moving a row into a tree folder, no dragging a tab.
Docking panels need exactly that, plus a layout that can change at run time and be saved.

What exists to build on:

- **Pointer capture.** A press on a widget captures the pointer (`WidgetTree::captured_id`); the
  split handle and the scrollbar thumb already turn a press-move-release into a drag.
- **An overlay layer.** Tooltips and popups draw on top of everything and are hit-tested first.
- **Subtrees built on demand.** Tab pages and popup content come from a builder function when
  shown (`TabsState::tab_node_builders`, `WidgetNode::popup_builder`).
- **Saved state.** `app::SettingsFile` saves fields as TOML; split sizes are fields already.
- **Several windows.** `AppContext::open_window` (desktop only).

## Part 1: dragging inside the app

### What is dragged: data, not widgets

```cpp
struct DragData {
    std::string kind;   // what it is, for targets to accept or refuse: "row", "tab", "text"
    std::string text;   // what it says: a label, a path, the text itself
    std::uint64_t item = 0;   // which one, in the source's own numbering (a row index, a node id)
};
```

Plain data, so Python gets it as a dict and a drag can cross windows. A typed payload
(`std::any`) would read better in C++ but could not cross into Python; see open question 1.

### Who takes part

- **A source** says what a press-and-move on it would drag:
  `vb.widget(f).drag_source([&] { return DragData{"row", label, i}; })`. Lists, trees and tab
  bars get it built in (`vb.list(items).reorderable()`, a tree's `on_move`, `vb.tabs(...)`
  with `movable_tabs`).
- **A target** says what it accepts and what a drop does:
  `vb.widget(f).drop_target({"row"}, [&](const DragData& d, DropPlace at) { ... })`, where
  `DropPlace` is where in the widget it landed (before or after an item, inside it).
- **Text fields** accept `"text"` with no code: an in-app drop of text is delivered as the
  `TextDrop` the OS path already sends, so one code path serves both.

### How it runs

1. A press on a source captures the pointer, as today. Nothing else happens yet: a click
   stays a click.
2. Moving more than 4 points with the button held asks the source for its `DragData`. None:
   no drag (the widget keeps the move, as a slider does).
3. The tree owns the drag from here (`DragState`: the data, the pointer, the target under
   it). The source gets no more moves. Escape cancels.
4. Each move hit-tests the snapshot. A target that accepts the kind gets `DragOver` with the
   place, and draws its own insertion marker; others get nothing, and the cursor says "no".
5. A small card with `text` follows the pointer, drawn in the overlay like a tooltip.
6. Release over an accepting target delivers `Drop`; elsewhere, nothing happens.

`DragOver`, `DragLeave` and `Drop` are input events the tree makes itself, never the backend:
the backend keeps sending only presses, moves and releases.

### Not in part 1

- **Between windows.** The pointer belongs to the window it was pressed in. SDL keeps
  reporting it while the button is held (mouse capture), so the tree could hand the drag over
  to another window; that waits for a use (floating panels, below).
- **Out to the OS** (dragging a file onto the desktop). SDL3 has no drag-source API.

## Part 2: docking

### The layout is a value the app owns

```cpp
struct DockGroup { std::vector<std::string> panels; std::size_t active = 0; };   // tabs
struct DockSplit { bool vertical; float ratio; std::size_t a, b; };              // two parts
struct DockLayout {                       // a binary tree, flattened: node 0 is the root
    std::vector<std::variant<DockGroup, DockSplit>> nodes;
    std::vector<std::string> floating;    // panels in windows of their own
};
core::Field<DockLayout> layout;           // in the app's settings struct: saved and restored
```

The app names its panels and says how to build each; the dock builds what the layout says:

```cpp
vb.dock(prefs.layout, {
    {"files",  [&](auto& vb) { vb.tree(files); }},
    {"editor", [&](auto& vb) { vb.widget(body); }},
    {"search", [&](auto& vb) { vb.vstack(search, results); }},
});
```

A panel missing from the layout (a new one, or an old settings file) goes into the first
group; a name in the layout with no panel is dropped. So a settings file from another version
always gives a working layout.

### Rearranging

Dragging a group's tab is a Part 1 drag of kind `"panel"`. Over a group, the overlay shows five
drop zones: its centre (join this group's tabs) and its four edges (split it there). A drop
changes the `DockLayout` value; the dock then rebuilds the parts of the view that moved, the
way a tab page is built when selected. Splitters between groups are the existing split handles,
bound to `DockSplit::ratio`.

What survives a move: everything in fields (text, selection in a list, a plot's data). What
does not: state kept in the widget itself, such as a scroll position or a caret. See open
question 2.

### Floating panels

"Float" (a group's menu, or a drop outside the window) moves the panel's name to
`layout.floating` and opens it with `AppContext::open_window`; closing that window puts it back
into a group. Desktop only, as open_window is. Dragging it back in needs the cross-window drag
above.

## Order of work, with estimates

1. Drag core and list reordering, with tests through `route_event` and a look in the browser
   (2–3 days).
2. Tree moves and tab reordering (1–2 days).
3. The dock: `DockLayout`, building groups and splits from it, saved with the settings; no
   dragging yet (3–4 days).
4. Moving panels with drop zones (2–3 days).
5. Floating panels (2 days).
6. Drags between windows, when 5 wants them back.

The System Monitor is the test app: its processes, graphs and log as dockable panels.

## Open questions

1. **The payload.** `DragData` (kind, text, item) is plain and crosses into Python. Is that
   enough, or should C++ also carry a typed value (`std::any`) beside it?
2. **Moving widgets, or rebuilding them.** Rebuilding a moved panel is simple and is how tabs
   work today, but it loses scroll positions and carets. Moving the built subtree keeps them,
   and needs the tree to re-parent nodes and re-index them. Rebuild first, move later if it
   shows?
3. **Where the layout lives.** Proposed: a `Field<DockLayout>` the app owns, saved with its
   settings. The alternative, the dock keeping it, is simpler for small apps but hides it from
   the app's code and its settings file.
