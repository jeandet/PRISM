# Drag and drop inside the app, and docking

**Status:** design agreed 2026-11-13 (the three open questions are decided, see the end). Part 1
core done 2026-11-13: `LeafHandle::drag_source` / `drop_target`, `ui::DropPlace`, the drag state
in `WidgetTree` (`arm_drag`, `start_drag_at`, `takes`, `end_drag`, `draw_drag`), routing in
`app/event_routing.hpp` (`route_drag_move`, `route_drag_release`), `CursorShape::NotAllowed`;
tests in `tests/test_drag.cpp`. Reorderable lists followed: `vb.list(items).reorderable()`,
`List::move`, `DragRole::between` (an insertion line instead of an outline). Python too:
`vb.widget(h, drag=, drop=, accepts=)` and `vb.list(h, reorderable=True)`, through
`ViewBuilder::with_drag`; a Python value travels as a shared pointer to a GIL-safe holder.
Trees followed: `TreeController::on_move(node, target, TreeDrop)`, `can_move`, `drop_zone`,
`DragRole::inside_middle` (an outline on a folder's middle, a line near its edges). List and tree
row clicks now land on release too, so pressing a row to drag it no longer collapses it. Tab
reordering is left to the dock's tab groups (part 2).

Part 2, step 3 done 2026-11-13: `ui/dock.hpp` (`DockNode`, `DockLayout`, `dock_group`,
`dock_split`, `fitted`, `same_structure`), `ViewBuilder::dock` building splits and tab groups
through an ordinary ViewBuilder, `WidgetNode::build_content` / `content_stale` (a container
rebuilt while the app runs, as tab pages are), settings writing a plain struct whole. Sizes and
shown tabs are written back without a rebuild. The System Monitor is a dock. Next: step 4,
moving panels (tabs dragged onto drop zones), keeping their widgets. Plan items P1 #6 and #7 in `doc/review-2026-11-12.md`.

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

### What is dragged: a typed value

A drag carries any C++ value (Python: any Python object), and a target accepts it **by type**,
as Flutter's `Draggable<T>` / `DragTarget<T>` and SwiftUI's `.draggable` /
`.dropDestination(for:)` do. Those typed APIs are the well-liked ones; string-keyed payloads
(Qt's `QMimeData`, HTML5's `DataTransfer`) are the ones developers work around. A drag stays
inside the process (SDL cannot export one to the OS), so no text form is needed yet.

What the pointer carries on screen is the source widget itself, drawn faded under the pointer:
no preview to write.

### Who takes part

- **A source** says what a press-and-move on it would drag:
  `vb.widget(f).drag_source([&] { return Track{id}; })`. Lists, trees and tab
  bars get it built in (`vb.list(items).reorderable()`, a tree's `on_move`, `vb.tabs(...)`
  with `movable_tabs`).
- **A target** says what it accepts and what a drop does:
  `vb.widget(f).drop_target([&](const Track& t, DropPlace at) { ... })`: the parameter's type
  is what it accepts, and `DropPlace` is where in the widget it landed. A drag of another type
  shows the "no" cursor over it, never a silent nothing (SwiftUI's trap).
- **Text fields** accept a `std::string` with no code: it is delivered as the `TextDrop` the OS
  path already sends, so one code path serves both.

### How it runs

1. A press on a source captures the pointer, as today. Nothing else happens yet: a click
   stays a click.
2. Moving more than 4 points with the button held asks the source for its value. None:
   no drag (the widget keeps the move, as a slider does).
3. The tree owns the drag from here (`DragState`: the data, the pointer, the target under
   it). The source gets no more moves. Escape cancels.
4. Each move hit-tests the snapshot. A target that accepts the kind gets `DragOver` with the
   place, and draws its own insertion marker; others get nothing, and the cursor says "no".
5. The source widget, faded, follows the pointer, drawn in the overlay like a tooltip; an
   accepting target under it gets an accent outline.
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

What survives a move: everything. The dock keeps each panel's built widgets, keyed by its
name, and a layout change only changes where they are placed: scroll positions, carets and
selections stay. Qt and its docking replacements (KDDockWidgets, Qt Advanced Docking) move the
real widget for the same reason, and Dear ImGui keeps state under the window's ID; web docking
libraries that rebuilt or moved page elements lost state (an embedded page reloads on every
move) and had to add "virtual" panels to stop it.

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

## Decisions (2026-11-13)

From what other toolkits do and what their users report:

1. **The payload is a typed value**, matched by type (Flutter, SwiftUI), not a string-keyed
   bundle (Qt `QMimeData`, HTML5 `DataTransfer`, widely criticized).
2. **A moved panel keeps its widgets** (Qt, KDDockWidgets, ImGui's ID-keyed state); rebuilding
   is what web docking libraries regretted.
3. **The layout is a value the app owns**, `Field<DockLayout>`, saved with its settings, with a
   default in code. Qt's opaque `saveState()` blob is its most complained-about part (floating
   and tabbed docks not restored, private format); ImGui users had to store named layouts and a
   "reset layout" themselves; the web libraries' JSON layouts are liked.
