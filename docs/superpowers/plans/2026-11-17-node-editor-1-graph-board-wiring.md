# Node editor, part 1: graph, board, wiring — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A working node editor without zoom or final look: nodes (real PRISM blocks) on a board,
moved by their header, panned, wired by dragging between typed ports, with values flowing
along the wires.

**Architecture:** A pure graph model (`prism::nodes`: port types, typed ports over an erased
`Port`, `Graph` owning `Edge`s) tested headless; a new `Board` layout kind in the engine that
places children at their natural size at given points and draws wires after layout, where
port rectangles are known; `vb.node_board(graph)` and `vb.port(p)` on top, using the existing
drag-and-drop for wiring.

**Tech Stack:** C++26 (works without reflection), Meson, doctest, PRISM's ViewBuilder, layout
and drag-and-drop.

**Spec:** `docs/superpowers/specs/2026-11-17-node-editor-design.md` (goals 1–4, 8, 11; the
"How it fits PRISM" section). Later plans: 2 zoom, 3 look/themes/probes, 4 Signal Lab,
5 Python.

## Global Constraints

- AGENTS.md rules: TDD (test fails first), non-template code in `src/<module>/`, no bare
  `detail` namespace (use `nodes_detail`), strong geometry types (`X`, `Y`, `DX`, `Point`,
  `Rect`), `simplify:` comment on each deliberate cut, no mutable `inline` variable in a header.
- No reflection required anywhere: every test must also compile with GCC without
  `-freflection` and with clang+libc++ (`bash ~/syntax_checks.sh <files>` on Coder).
- Builds and tests run on the Coder workspace through `/tmp/claude-6516/prism_remote.sh '<cmd>'`,
  one build at a time, in the foreground. Full suite before each commit:
  `ninja -C builddir && meson test -C builddir --no-rebuild -q` → `Fail: 0`.
- New headless tests are registered in `tests/meson.build`'s `headless_tests` map.
- Moving a node, panning, or a value flowing along a wire must not rebuild any node's widgets
  (persistent tree); only adding/removing a node or wire changes the board's structure.
- Nodes are owned by the app; the graph references them. The app removes a node from the
  graph before destroying it.
- An input takes at most one wire; connecting another replaces it. An unconnected input holds
  its default value; disconnecting reverts it to the default.

## Review Focus

1. Removing a node that has wires: its edges go, the inputs they fed revert to their defaults,
   and a later change on the removed node's outputs reaches nobody. (Task 3.)
2. Destroying the `Graph` while nodes still live (app shutdown in either order): no callback
   into a freed edge. (Task 3.)
3. A wire from a node's output back to one of its own inputs, or one closing a longer loop:
   refused with `ConnectError::Cycle`, nothing changed. (Task 3.)
4. A wire dropped on an incompatible port, on an output, or on the port it started from:
   refused, graph unchanged, no crash. (Task 7.)
5. A node panned or moved partly or wholly off the board: drawn clipped, still wired, and
   back in full view after panning back; negative positions are valid. (Tasks 4 and 6.)

---

### Task 1: Port types and ports

**Files:**
- Create: `include/prism/nodes/port.hpp`
- Create: `src/nodes/port_type.cpp` (add to `src/meson.build`'s libprism sources)
- Test: `tests/test_node_ports.cpp`

**Interfaces:**
- Produces:
  - `namespace prism::nodes`
  - `using TypeKey = std::uint32_t;`
  - `template <class T> struct PortType;` — a trait with `static constexpr std::string_view name`;
    specializations shipped for `double` ("number"), `int` ("integer"), `bool` ("boolean"),
    `std::string` ("text"), `std::vector<double>` ("waveform"). An app's own type specializes it.
  - `TypeKey type_key(std::string_view name);` (src: one registry, name → key, same key for the
    same name from any shared object) and `template <class T> TypeKey type_key();` (calls the
    first with `PortType<T>::name`).
  - `std::string_view type_name(TypeKey);`
  - `enum class Dir { In, Out };`
  - `class Port` (erased): `Dir dir() const`, `TypeKey type() const`, `std::string_view name() const`,
    `virtual std::any value() const = 0`, `virtual void set_value(const std::any&) = 0` (inputs
    only; asserts on an output), `virtual void reset() = 0` (inputs: back to the default),
    `virtual core::Connection on_change(std::function<void()>) = 0`.
  - `template <class T> class In : public Port` — `In(std::string name, T fallback = {})`;
    `const T& get() const` (reads a `core::Field<T>`, so a `Derived` reading it tracks it);
    the field is `In::field` (public, so a node's `view()` may show it).
  - `template <class T> class Out : public Port` — `Out(std::string name, std::function<T()> compute)`
    over a `core::Derived<T>` (it follows what `compute` reads); `const T& get() const`.

- [ ] **Step 1: Write the failing tests** in `tests/test_node_ports.cpp`:
  - `"a type's key is the same however it is asked for"`:
    `CHECK(type_key<double>() == type_key("number"))`, `CHECK(type_key<double>() != type_key<int>())`,
    `CHECK(type_name(type_key<std::vector<double>>()) == "waveform")`.
  - `"an output computes from an input and follows it"`: `In<double> a{"a", 1.0}`;
    `Out<double> twice{"twice", [&] { return a.get() * 2; }}`; `CHECK(twice.get() == 2.0)`;
    `a.set_value(std::any(4.0))`; `CHECK(twice.get() == 8.0)`.
  - `"an input reverts to its default on reset"`: `In<int> n{"n", 7}`; `n.set_value(std::any(3))`;
    `n.reset()`; `CHECK(n.get() == 7)`.
  - `"an erased port reports its direction, type and value"`: through `Port&`:
    `dir() == Dir::Out`, `type() == type_key<double>()`, `std::any_cast<double>(p.value()) == 2.0`.
  - `"on_change fires when the value changes"`: count calls through `Port::on_change` on an `Out`
    when its input changes: `1`.
- [ ] **Step 2: Run, expect a compile failure** (`port.hpp` missing):
  `ninja -C builddir tests/test_node_ports`
- [ ] **Step 3: Implement** `port.hpp` and `port_type.cpp`. The registry is a function-local
  `static` in `port_type.cpp` (a map name → key, keys from 1) behind a mutex.
- [ ] **Step 4: Run** `builddir/tests/test_node_ports` → `Status: SUCCESS`.
- [ ] **Step 5: Commit** `feat(nodes): typed ports over an erased Port, port types by name`.

### Task 2: Wire geometry

**Files:**
- Create: `include/prism/nodes/wire_geometry.hpp` (header-only, pure)
- Test: `tests/test_wire_geometry.cpp`

**Interfaces:**
- Produces: `struct WireCurve { core::Point p0, c0, c1, p1; };`
  `WireCurve wire_curve(core::Point from, core::Point to);` — horizontal tangents, control
  distance `max(40, |to.x − from.x| / 2)`;
  `core::Point point_on(const WireCurve&, float t);`
  `std::vector<core::Point> polyline(const WireCurve&, int segments = 24);`
  `float distance_to(const WireCurve&, core::Point p);` (to the polyline).

- [ ] **Step 1: Failing tests:** `"a wire leaves and enters horizontally"`
  (`c0.y == p0.y`, `c1.y == p1.y`, `c0.x > p0.x`, `c1.x < p1.x`); `"its ends are its points"`
  (`point_on(w, 0) == from`, `point_on(w, 1) == to`); `"a point on the wire is at distance 0,
  one 10 above a straight wire at 10"` (straight wire (0,0)→(100,0): `distance_to(w, {50,0}) <
  0.5`, `distance_to(w, {50,-10}) == Approx(10).epsilon(0.05)`); `"a backward wire still
  curves out of its port"` (from (100,0) to (0,50): `c0.x >= 140`).
- [ ] **Step 2: Run** → compile failure. **Step 3: Implement.** **Step 4: Run** → pass.
- [ ] **Step 5: Commit** `feat(nodes): wire curves and their hit distance`.

### Task 3: The graph

**Files:**
- Create: `include/prism/nodes/graph.hpp`, `src/nodes/graph.cpp`
- Test: `tests/test_node_graph.cpp`

**Interfaces:**
- Consumes: Task 1's `Port`, `type_key`.
- Produces:
  - `using NodeId = std::uint32_t; using EdgeId = std::uint32_t;`
  - `struct PortRef { NodeId node; std::size_t port; bool operator==(const PortRef&) const = default; };`
    (`port` indexes the node's `ports()` list).
  - `template <class N> concept NodeType = requires(N& n) { { n.ports() } -> std::convertible_to<std::vector<Port*>>; };`
    A node struct lists its ports: `std::vector<nodes::Port*> ports() { return {&a, &b, &out}; }`.
  - `struct NodeOptions { std::string title; std::string family; core::Point at; bool removable = true; };`
  - `enum class ConnectError { NotOutputToInput, TypeMismatch, Cycle, UnknownPort };`
  - `class Graph` (non-copyable, non-movable):
    - `template <NodeType N> NodeId add(N& node, NodeOptions opt);` — stores a type-erased entry
      holding `&node`, its ports, a `std::function<void(app::ViewBuilder&)>` that calls
      `node.view(vb)` when `N` has one, the options.
    - `void remove(NodeId);` — removes its edges first (reverting the inputs they fed).
    - `std::expected<EdgeId, ConnectError> connect(PortRef a, PortRef b);` — `a` and `b` in either
      order (one output, one input); replaces an existing edge into that input; copies the
      value at once.
    - `bool can_connect(PortRef a, PortRef b) const;` (same checks, no change)
    - `void disconnect(EdgeId);` — reverts the input.
    - `std::optional<EdgeId> edge_into(PortRef input) const;`
    - `struct EdgeInfo { PortRef from, to; };` `const EdgeInfo* edge(EdgeId) const;`
    - `std::vector<EdgeId> edges() const; std::vector<NodeId> nodes() const;`
    - `Port* port(PortRef) const;` `const NodeOptions& options(NodeId) const;`
    - `void move(NodeId, core::Point at);` `core::Point position(NodeId) const;`
    - `core::Field<std::uint64_t> structure;` bumped when a node or edge is added/removed;
      `core::Field<std::uint64_t> layout;` bumped on `move` and on pan (Task 6).
  - An `Edge` (graph-private, in `graph.cpp`) holds `from`, `to` and the `Connection` from the
    output's `on_change` that calls `to.set_value(from.value())`.
    `// simplify: values cross as std::any copies, synchronously; the Edge is the seam where a
    later version hands them to another thread (spec goal 11).`

- [ ] **Step 1: Failing tests** (nodes are tiny local structs with `In`/`Out` and `ports()`):
  - `"a wire carries the output's value now and on each change"`
  - `"connect takes the ends in either order"`
  - `"an output feeds several inputs; a second wire into an input replaces the first"`
    (`edges().size() == 2` after three connects; the replaced edge id no longer valid).
  - `"a wire between different types is refused"` → `ConnectError::TypeMismatch`, `edges()` empty.
  - `"two outputs or two inputs cannot be wired"` → `NotOutputToInput`.
  - `"a wire that closes a loop is refused, even through other nodes"` (A.out→B.in, B.out→A.in
    → `Cycle`; and a node's output to its own input → `Cycle`).
  - `"disconnecting reverts the input to its default"`
  - `"removing a node removes its wires and reverts the inputs they fed"`; then setting the
    removed node's source field changes nothing downstream.
  - `"destroying the graph first leaves the nodes usable"`: graph in a nested scope; after it,
    changing an upstream value does not crash (run under the asan build in CI).
  - `"structure changes on add, connect, disconnect and remove; layout on move"`.
- [ ] **Step 2: Run** → compile failure.
- [ ] **Step 3: Implement.** Cycle check: from `to.node`, follow edges forward (output → inputs);
  reaching `from.node` is a cycle.
- [ ] **Step 4: Run** → pass. Full suite.
- [ ] **Step 5: Commit** `feat(nodes): a graph of nodes and typed wires that refuses loops`.

### Task 4: The Board layout kind

**Files:**
- Modify: `include/prism/ui/delegate.hpp` (`LayoutKind::Board`), `include/prism/ui/layout.hpp`
  (`LayoutNode::Kind::Board`, measure, arrange, flatten), `include/prism/ui/node.hpp` and
  `include/prism/ui/widget_node.hpp` (fields below), `include/prism/app/widget_tree_layout.hpp`
  (copy them to the layout node as `layer_places` is), `src/app/widget_tree.cpp`.
- Test: `tests/test_board_layout.cpp`

**Interfaces:**
- Produces, on `Node`/`WidgetNode`/`LayoutNode`:
  - `std::function<std::vector<core::Point>()> board_places;` — where children after the first
    lie, in the board's coordinates (read at every layout: moving needs no rebuild).
  - `using BoardRectOf = std::function<std::optional<core::Rect>(WidgetId)>;` (a descendant's
    rect in the board's coordinates, from this layout)
  - `std::function<void(render::DrawList&, const BoardRectOf&)> board_under;` — drawn after the
    first child (the background), before the others.
  - `std::function<void(render::DrawList&, const BoardRectOf&)> board_over;` — drawn after all.
- Behaviour: the first child fills the board; each other child is laid out at its natural size
  (what `layout_measure` gives it, as a column's child) at its point, not clamped; everything is
  clipped to the board's rect. The board measures as `expand`.

- [ ] **Step 1: Failing tests** (build a `LayoutNode` tree directly or through a tiny model with
  `vb` calls once Task 5 exists; here directly): `"a board's cards keep their natural size at
  their points"`; `"a card at a negative point or past the edge is placed there, clipped"`
  (geometry origin negative; its draws are inside a clip to the board's rect);
  `"board_under is drawn above the background and below the cards; board_over above all"`
  (z order of the three draw lists); `"rect_of gives a card's descendant's rect in board
  coordinates"`.
- [ ] **Step 2: Run** → fail. **Step 3: Implement** in measure/arrange/flatten following the
  `Layers` cases. **Step 4: Run** → pass; full suite.
- [ ] **Step 5: Commit** `feat(ui): a Board layout: cards at points, natural size, drawn over and under`.

### Task 5: `vb.node_board` and `vb.port`

**Files:**
- Create: `include/prism/app/node_board.hpp` (the templates), `src/app/node_board.cpp`
- Modify: `include/prism/app/view_builder.hpp` (declare `node_board`, `port`)
- Test: `tests/test_node_board.cpp`

**Interfaces:**
- Consumes: Task 3 `Graph`, Task 4 board fields.
- Produces:
  - `WidgetId ViewBuilder::node_board(nodes::Graph& graph);` — a Board: a background canvas
    child, then one card per node (in `graph.nodes()` order) with a header row (title; family
    as text for now — colours are plan 3) and the node's `view()` below. Rebuilt when
    `graph.structure` changes (a dependency), relaid out when `graph.layout` changes.
    `board_places` reads `graph.position(id) + pan`.
  - `LeafHandle ViewBuilder::port(nodes::Port& p);` — a row widget showing the port's name,
    left-aligned for an input, right-aligned for an output; records, per board, port → its
    `WidgetId` so `board_under` finds its rect.
  - The board's `board_under` draws each edge as `polyline(wire_curve(out_end, in_end))`, where
    an output's end is the middle of its card's right edge at the port row's centre y, and an
    input's the left edge (theme accent colour for now).
  - `board_over` draws each port's dot (radius 5) at those ends.
- [ ] **Step 1: Failing tests** (a model with a `Graph`, two nodes with a port each, `vb.node_board`):
  `"each node is a card with its title and its widgets"` (texts in the snapshot);
  `"a wire is drawn from the output's card edge to the input's"` (a polyline whose first point
  is the output card's right edge at the port row's y and last the input card's left edge);
  `"moving a node relays it out without rebuilding its widgets"` (widget ids unchanged,
  card origin moved by the delta); `"connecting rebuilds nothing but adds a wire"`.
- [ ] **Step 2: Run** → fail. **Step 3: Implement.** **Step 4: Run** → pass; full suite;
  syntax checks on the new files.
- [ ] **Step 5: Commit** `feat(app): vb.node_board and vb.port: a graph drawn as cards and wires`.

### Task 6: Moving nodes and panning

**Files:**
- Modify: `src/app/node_board.cpp`, `include/prism/nodes/graph.hpp` (`core::Point pan;`
  `void pan_by(core::DX, core::DY)` bumping `layout`)
- Test: `tests/test_node_board.cpp`

**Interfaces:**
- Produces: dragging a card's header moves its node (`graph.move`), the press point staying
  under the pointer; dragging the empty background pans every node; the header and the
  background take the mouse with the usual capture.
- [ ] **Step 1: Failing tests:** `"dragging a header by (30, 20) moves its node by (30, 20)"`
  (route MouseButton/MouseMove/MouseButton through `route_event`, snapshot between, as in
  `test_list_selection.cpp`); `"dragging the background pans all nodes"`; `"a node panned off
  the board and back is where it was"` (Review Focus 5).
- [ ] **Step 2–4:** fail → implement → pass; full suite.
- [ ] **Step 5: Commit** `feat(app): move nodes by their header, pan the board`.

### Task 7: Wiring by drag

**Files:**
- Modify: `include/prism/ui/node.hpp` (`DragRole`: add
  `std::function<void(render::DrawList&, core::Point from, core::Point pointer)> trail;` — drawn
  instead of the ghost while this source's value is dragged; and `bool hint_targets = false;` —
  while dragging, every target that accepts the value shows a soft marker, not only the one
  under the pointer), `src/app/widget_tree.cpp` (`draw_drag`), `include/prism/app/drag_draw.hpp`
  (the soft marker), `src/app/node_board.cpp`.
- Test: `tests/test_node_board.cpp`, `tests/test_drag.cpp` (the two DragRole additions)

**Interfaces:**
- Produces: a port is a drag source of `struct PortDrag { nodes::Graph* graph; nodes::PortRef from; };`
  and a drop target accepting a `PortDrag` when `graph->can_connect(from, this)`; `hint_targets`
  on; `trail` draws the wire curve from the port's end to the pointer. Dragging from an input
  that has a wire picks up that wire: its edge is removed and the drag carries its output end
  (let go on nothing: the wire is gone). Clicking within 6 px of a wire selects it (drawn
  thicker); Delete or Backspace removes the selected wire, or the selected node when
  `removable`. A header click selects its node.
- [ ] **Step 1: Failing tests:** in `test_drag.cpp`: `"a source with a trail draws it instead
  of a ghost"`, `"hint_targets marks every accepting target"`. In `test_node_board.cpp`:
  `"dragging from an output onto a compatible input connects them"`; `"a drop on an
  incompatible input, on an output, or on the starting port changes nothing"` (Review Focus 4);
  `"dragging a wire off its input and letting go on the board removes it"`; `"click a wire, press
  Delete: it is gone"`; `"a node that is not removable stays on Delete"`.
- [ ] **Step 2–4:** fail → implement → pass; full suite; syntax checks.
- [ ] **Step 5: Commit** `feat(app): wire nodes by dragging between ports`.

### Task 8: A showcase and a check in the browser

**Files:**
- Create: `examples/showcase/showcase_node_board.cpp` (register in `examples/showcase/meson.build`;
  a sine node and a gain node wired to a value display; 640×300)
- Modify: `doc/screenshots/node_board.svg` (generated)

- [ ] **Step 1:** Write the showcase; build it; copy the SVG; look at it (contact sheet as in
  this session's earlier checks).
- [ ] **Step 2:** Put a board in a scratch gallery build for the browser (not committed) and
  drive it with `pw/shot.js`: move a node, pan, wire two ports, unplug a wire. Fix what is
  wrong, each with a test first.
- [ ] **Step 3: Commit** `docs(showcase): a node board`.
