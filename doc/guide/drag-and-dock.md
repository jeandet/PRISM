# Drag and drop, and docking

## Drag and drop inside the app

A widget can be dragged, and another can take what is dropped on it. The value is typed: a
target takes the type its callback names and refuses others with a no-entry cursor. While
dragging, the source follows the pointer, faded, and a target that takes it is outlined. A
dragged `std::string` dropped on a text field is typed into it. Escape cancels.

```cpp
struct Track { int id; };
vb.widget(song).drag_source([&] { return Track{song_id}; });
vb.widget(playlist).drop_target([&](const Track& t, ui::DropPlace at) { add(t, at); });
```

A list can be reordered by dragging its rows, `vb.list(items).reorderable()`; an insertion line
shows where a row will go, and `List::move` does the move. A tree's nodes can be dragged once its
controller has `on_move(node, target, where)`: dropped on the middle of a folder a node goes
inside it, near an edge before or after; never into its own subtree. The app moves it in its
data and the tree refreshes.

A list keeps a selected row in an app field, `vb.list(items).selection(selected)`
(`Field<std::optional<size_t>>`): a click selects a row, Up/Down/Home/End move the selection and
scroll it into view, and a removal keeps it on the same item.

A click lands on the release, over the widget that was pressed, as on every platform: sliding
off a button before letting go cancels it, and a drag that starts on a button is not a click.

## Docking panels

A dock arranges named panels in splits and tab groups. The arrangement is a value the app owns,
`core::Field<ui::DockLayout>`, kept in its settings; the user's split sizes and tab choices are
written back into it. A saved layout from another version still works: panels it does not name
join its first group, names with no panel are dropped.

```cpp
core::Field<ui::DockLayout> layout{{ui::dock_split(/*vertical=*/true, {
    ui::dock_group({"plots"}), ui::dock_group({"table", "tree", "events"})})}};
vb.dock(layout, {
    {"plots", "Plots", [&](auto& p) { plots.view(p); }},
    {"table", "Table", [&](auto& p) { p.table(processes); }},
    ...
});
```

Drag a panel's tab onto a group: its middle adds the panel to that group's tabs, an edge puts
it beside the group in a new one. The area it would take is shaded while dragging. Groups left
empty disappear. A moved panel keeps its widgets: its scroll position, caret, selection and
focus stay, as with Qt's dock widgets. Let a tab go outside the window and its panel floats in
a window of its own (desktop); close that window and the panel docks again. Drag a tab along its
bar to put it elsewhere among the tabs. A double click on a tab, or the button at the right end
of its bar, floats the panel too. In the browser, where the page is the only window, a floating
panel is a card over the dock instead: drag its title to move it; let it go on a guide's button
to dock it there, or press its button to dock it back.
A panel declared closable (`{"log", "Log", build, true}`)
gets a close button; `ui::show_panel(layout, "log")` brings it back, from a View menu say. The
System Monitor is built this way.

Tabs can also stand for the items of a list, an editor's open files say. Closing a tab or
dragging it along the bar changes the list:

```cpp
vb.tabs(shown, open_files, [](const File& f) { return f.name; },
        [&](auto& p, const File& f) { editor_for(p, f); })
    .closable()
    .reorderable();
```
