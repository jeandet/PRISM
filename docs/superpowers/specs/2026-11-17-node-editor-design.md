# Node editor — design

Date: 2026-11-17. Status: approved approach, look chosen; this document is the design.

## Intent

PRISM gets a reusable node editor: blocks on a board, joined by wires between their ports,
values flowing along the wires. The user wants it as the ground for a future dataflow tool in
the spirit of LabVIEW ("my own LabVIEW killer"), so it must hold up as a real editor, not a demo.
Its first user is **Signal Lab**, a puzzle in the widget gallery: wire signal blocks (sine,
noise, filter, mixer) so the scope's output matches a target waveform.

Two things the user called out:

- **It must look good.** The chosen look is in the mockup canvas
  (https://claude.ai/artifact/6GXkvgxTM24hLRBHjKrfGp): look A ("Instrument") in the dark
  theme, look B ("Paper") in the light theme — the same structure, colours from the theme —
  with look C's idea kept: real widgets inside the nodes.
- **Zoom is essential** ("LabVIEW never had it"). It is in the first version, not a follow-up.

## Goals

1. **Nodes are ordinary PRISM blocks.** A node is a struct with fields, a `view()`, and ports.
   Its controls are real widgets (a cutoff is a real slider: focus, keys, tooltip, a11y).
2. **Typed ports and wires.** A port has a C++ value type. A wire joins an output to an input
   of a compatible type; while the user drags a wire, compatible ports light up and the others
   dim, and a drop on an incompatible one is refused. The wire's colour says its type.
3. **A connection is a real link.** Connecting copies the output's value into the input now and
   on every change (the existing `core::Wiring::link` semantics). Disconnecting stops it.
4. **Editing:** move a node by dragging its header; drag from a port to another to wire; drag
   a wire's end off its input to disconnect (or onto another input to move it); click a wire
   to select it, Delete removes it; select a node, Delete removes it and its wires (when the
   app allows removal).
5. **Pan and zoom.** Drag the empty board (or middle-drag anywhere) to pan; Ctrl+wheel (and
   pinch where the platform reports it) zooms around the pointer, from 25 % to 200 %; a
   "fit all" action. Widgets inside zoomed nodes stay real and usable at any zoom: they are
   drawn scaled (text at the scaled size, never a scaled bitmap) and the pointer is turned back
   into their own coordinates.
6. **Probes.** Hovering a wire shows its current value in a bubble (look A's "probe"). A value
   is shown through a formatter for its type; a type with none shows its type name.
7. **The look.** Node header coloured by family (source, filter, math, display, and any family
   an app adds), live previews inside nodes are just the node's own widgets/canvas, wires drawn
   as smooth curves (cubic Bézier, horizontal tangents), selection glow, dot grid background in
   dark, line grid in light. All colours are new `ui::Theme` roles, in `dark_theme()` and
   `light_theme()`.
8. **No reflection required.** Everything works where reflection is missing (the browser
   build, clang): a node's `view()` places its ports, as it places its widgets.
9. **Signal Lab** in the gallery: a few levels, each a fixed set of nodes and a target waveform;
   the player wires and tunes; the scope shows target vs output and a match score; a level is
   won at a threshold the builder tunes (around 95 %).
10. **Python.** Nodes, their ports and their controls can be written in Python (the user:
    "that would be awesome"), and a Python app can build and edit a graph. So port types are
    registered at run time (a C++ type registers itself; a Python type registers too), and the
    graph has a type-erased API the bindings call; the C++ `In<T>`/`Out<T>` are typed sugar
    over it. A Python value crosses a wire as a Python object (a port type of its own).
11. **Room for nodes that compute off the window's thread** (the user: yes). The first version
    delivers values synchronously, but a wire is an edge object the graph owns, not a bare
    `Field` link, so an edge can later hand its value across threads (`Shared`/`Channel`) and a
    slow node (an FFT, an instrument read) can run on a worker without changing `connect()` or
    the port types.
12. **Documented** in the manual (a widget page under "Data views" or its own family, with a
    showcase screenshot), like every widget.

## Non-goals (for this first version, but the design must not block them)

- Saving and loading a graph to a file. Positions should already be plain data (so a later
  save is a serialization, not a redesign).
- A palette or "add node" menu. Nodes are added by the app (Signal Lab: per level). The graph
  API must allow adding and removing nodes at run time, so a palette is only UI.
- Sub-graphs (a node made of a graph), loops/structures (LabVIEW's while/for/case frames),
  execution highlighting.
- Multi-selection, rubber-band selection, copy/paste, undo of graph edits.
- Wire routing around nodes (wires are curves; they may cross nodes).

## Anti-goals (would make it a failure even if it works)

- Controls drawn by hand inside nodes (approach 2 below): no focus, keys or a11y.
- Zoom that scales a bitmap (blurry text) or that breaks clicking widgets inside nodes.
- A graph that only works with reflection.
- Cycles that hang the app: a wire that would close a cycle is refused (until a deliberate
  feedback node exists).
- Wires or ports reaching into a node's internals: a node exposes only its ports.

## Constraints

- Follow AGENTS.md: TDD, pure logic separated from widgets and tested headless (graph model,
  type compatibility, cycle detection, wire geometry and hit distance, zoom transform math),
  strong geometry types, `simplify:` comments on deliberate cuts, no bare `detail` namespace,
  non-template code in `src/`.
- Persistent widget tree: moving a node, panning or zooming must not rebuild the node's widgets
  (it changes their placement only). Adding/removing nodes rebuilds the board's children.
- Performance target: 200 nodes and 300 wires pan and zoom smoothly (one frame per refresh,
  now that the clock is paced); measure with a perf_lab-style headless count.
- Works on desktop and in the browser.

## Approaches considered

1. **Nodes are real blocks on a board (chosen).** Each node a block; the board places them at
   free positions (as `vb.layers` places floating cards); a new board layer draws wires and
   handles port drags; ports wrap fields; a connection is a link. Zoom through an engine-level
   transform of the board's subtree (below). Real widgets everywhere; reuses blocks, drag
   roles and links. Cost: the zoom transform in the engine.
2. **One custom widget drawing everything** (as the gallery's Orbit game). Pan/zoom trivial,
   but every control in a node hand-drawn: rejected (anti-goal).
3. **Approach 1 with zoom built into the layout engine as a general feature first.** Cleanest,
   but delays anything on screen. Folded into 1: the transform is built as a general
   "scaled subtree" mechanism, used first by the board.

The user picked 1, with zoom in the first version.

## How it fits PRISM (decided)

### Graph model (pure, `core`-like, tested headless)

- `Graph` owns the nodes and the wires. A node entry holds: the node block (type-erased, owned
  by the graph or referenced — builder's call), its position (plain data: a point in board
  units), its family, and its ports (discovered when its `view()` runs, see below).
- A wire is `{from: (node, output port), to: (node, input port)}`. An input takes at most one
  wire (connecting another replaces it); an output feeds any number.
- `connect(out, in)` checks: types compatible, no cycle (graph reachability from `in`'s node
  to `out`'s node), not already connected. On success it creates the link and returns a wire
  id; otherwise a reason (for the UI's refusal feedback).
- Each wire is an `Edge` the graph owns: it subscribes to the output and writes the input.
  v1 does it synchronously (as `Wiring::link` does); the edge is the seam where a later version
  hands the value to another thread. Nothing outside the graph links ports directly.
- Type compatibility: same type. (A conversion table — int to double, number to waveform — is a
  later extension; leave a seam.)
- Signals: the graph notifies on node added/removed/moved and wire added/removed, so the board
  redraws only what changed and an app (Signal Lab's scoring) can react.

### Ports

- `In<T>` and `Out<T>` are members of a node struct. `Out<T>` holds/forwards a value the node
  produces (a `Field<T>` or a `Derived<T>` underneath); `In<T>` holds a value a wire writes
  (a `Field<T>`, with a default when unconnected).
- A node's `view()` places its ports with the ViewBuilder (for example `vb.port(in_a)`), which is
  how the board learns where each port sits (its widget's rectangle) without reflection. Inputs
  draw on the node's left edge, outputs on the right, aligned with the row that places them.
- Port type identity: a runtime type key, from a registry filled while the program runs: each
  C++ `T` registers once (no RTTI requirement), and the Python bindings register Python port
  types the same way. The registry holds each type's colour and probe formatter, with defaults for `double`, `int`, `bool`, `std::string`,
  `std::vector<double>` ("waveform").

### Board (the widget)

- `vb.node_board(graph)` places the board. It is a container: its children are the nodes'
  blocks, each wrapped in a node frame (header with title and family colour, body = the node's
  `view()`), placed at the node's position × zoom + pan.
- Wires are drawn by the board in a layer above the background and below the nodes (probe
  bubble and the wire being dragged above the nodes).
- Interactions use the existing drag-and-drop machinery where it fits (ports as drag sources and
  targets with type-checked `takes_at`), and board-level pointer handling for pan, zoom, wire
  selection and node moves.
- View state (pan, zoom) is a field the app may save, like a dock layout.

### Zoom in the engine (new)

- A subtree may carry a transform: a scale `s` and an offset. Layout runs at natural size (s=1);
  flatten writes the subtree's geometry transformed (origin = offset + s·local, extent·s) and
  scales its draw lists (points, rects, radii, line thickness, text size, clip rects).
- The snapshot records each geometry entry's scale, so event routing turns the pointer into a
  widget's own coordinates as (p − origin) / s. Hit testing ("which widget") already works on
  the transformed rectangles.
- Text is drawn at the scaled size from the same layout; tiny metric differences between sizes
  are accepted (`simplify:` note: re-measure at the zoom level if overflow shows).
- Pure transform math and draw-list scaling are tested headless; a pixel test checks a zoomed
  node's slider is still dragged by its thumb.

### Theme roles (new)

Board background and grid, node surface and border, node header per family (a small table:
family → colour, extendable by the app), selection glow, wire colour per type (from the type
registry, themed), probe bubble, port fill/ring, dimmed (incompatible) port. Values for dark
and light following looks A and B.

### Signal Lab (gallery tab)

- Nodes: Sine (frequency, amplitude, phase), Noise (level), Low-pass (cutoff), Mixer (two
  inputs, two gains), Scope (shows input vs the level's target, and the match score).
- Waveforms are `std::vector<double>` of a fixed sample count over a fixed window; each node's
  output is a `Derived` of its inputs and controls, so the whole patch recomputes on any change.
- Match score: 100 % minus normalized RMS error, clamped; the level is won above the threshold.
- 3–5 levels; each defines its nodes, their starting positions and controls, and the target.
- Tested like Orbit: the DSP and scoring are pure functions with tests, and a test proves each
  level is winnable (a known solution scores above the threshold).

## Calls made in the design review (not the user's; check them)

- **Speed target:** 200 nodes / 300 wires smooth on the desktop. The browser is measured early
  in the zoom step; if flatten-time scaling is too slow there, the fallback is a scale applied
  by the renderer (a backend change), decided on that measurement.
- **Reflection:** v1 nodes place their ports in `view()`. An automatic view for a node struct
  without one (inputs left, outputs right, found by reflection) is a later extension; nothing
  in v1 may block it.

## Left to the builder

- Exact names (`Graph`, `In`/`Out`, `vb.port`, `vb.node_board`) and file layout (a new
  `include/prism/nodes/` module or under `widgets/`).
- Whether the graph owns node objects or references app-owned ones.
- Wire hit tolerance, curve tangent length, zoom step per wheel notch, exact colours within the
  looks.
- The probe formatter mechanism (`std::formatter` when available, else a registry function).
- How to split the work into commits; the order below is a suggestion.

## Suggested order

1. Graph model, ports, type keys, connect/disconnect, cycle check (pure, tested).
2. Board: nodes placed at positions, node frames, move by header drag, wires drawn.
3. Port drag to connect, typed feedback, disconnect, wire selection and delete.
4. Engine: scaled subtrees (transform in flatten, per-entry scale in routing), then pan/zoom
   on the board; pixel test for a widget in a zoomed node.
5. Theme roles and the A/B looks; probes.
6. Signal Lab tab with levels; showcase screenshot; manual page.
7. Python bindings: port types registered from Python, nodes written in Python, a Python
   example and tests.
