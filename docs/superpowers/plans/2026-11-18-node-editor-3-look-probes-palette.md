# Node editor part 3: the look, probes, and a palette — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The node board looks like the chosen mockups (look A in the dark theme, look B in the
light one), hovering a wire shows its value, and the user can add nodes from a palette and remove
them — the gallery's Nodes tab becomes a small GNU Radio–style signal sandbox.

**Architecture:** Every colour is a `ui::Theme` role, with values in `dark_theme()` and
`light_theme()`. A port type's wire colour and its probe text come from the port-type registry
(`prism::nodes`), filled at run time so Python types can join later. Adding and removing nodes is
already possible in the graph; this plan adds graph-owned nodes, a `NodeKind` (a named recipe that
adds one node), `vb.node_palette` (a list of kinds the user drags onto a board or activates), and a
board that takes those drops.

**Tech Stack:** C++26, Meson, doctest; SDL3 backend (desktop and Emscripten).

**Spec:** docs/superpowers/specs/2026-11-17-node-editor-design.md — goals 6 (probes), 7 (the look),
"Theme roles (new)", "Ports" (type registry: colour and probe formatter). The palette was a
non-goal of the spec's first version ("Nodes are added by the app… the graph API must allow adding
and removing nodes at run time, so a palette is only UI"); the user asked for it on 2026-11-18
("the example would be better if the user could add and remove nodes, like a small gnu-radio").
Parts 1 and 2: docs/superpowers/plans/2026-11-17-node-editor-1-graph-board-wiring.md,
docs/superpowers/plans/2026-11-18-node-editor-2-zoom.md.

**Mockups** (exact colours): look A, dark, "Instrument":
`/tmp/claude-6516/-home-jeandet-Documents-prog-PRSIM/7adc7c5d-99fb-4140-ba0f-16c5d76b6d4b/scratchpad/nodecanvas/project/Main.dc.html`;
look B, light, "Paper": `…/nodecanvas/project/Paper.dc.html` (same folder). Read the inline
styles; the values below are taken from them.

## Global Constraints

- Every colour the board draws is a `ui::Theme` role with a value in both `dark_theme()` (look A)
  and `light_theme()` (look B); no colour literal in node_board.cpp.
- Look A (dark) values: board `#0e1016` with a dot grid `#262a36` every 20 board units; card
  surface `#191c26`, border `#2a2f3d`, corner radius 10; selected card border `#3a4258` plus a
  2 px ring of the accent at 35 % alpha; family colours source `#3fd0c9`, filter `#a98bff`,
  math `#f2a33a`, display `#5fd38d`; header background = the family colour mixed 20 % over the
  surface; header family label = the family colour mixed 60 % over the text colour; probe bubble
  background `#1f2433`, border = the wire's colour, text `#cfe3ff`, radius 10; wire colours
  waveform `#4ea1ff`, number `#f2a33a`, boolean `#5fd38d`, integer `#a98bff`, text `#eef1f7`.
- Look B (light): board `#f5f4f0` with a line grid `#ebe9e3` every 20 board units; card surface
  `#ffffff`, border `rgba(20,22,30,0.12)`; family colours source `#1a9e8f`, filter `#7a5af8`,
  math `#d9822b`, display `#1f7a45`; accent and waveform wire `#2f6fde`; probe bubble background
  `#ffffff`, text `#24262b`. Mixing rules as in look A. Values not in the mockup: the builder
  picks them in the same spirit and lists them in the report.
- An app adds a family colour without touching node_board (a theme value it sets), and a type
  colour and probe text without touching node_board (the registry).
- Probes: "Hovering a wire shows its current value in a bubble… A value is shown through a
  formatter for its type; a type with none shows its type name."
- Python later: port types, formatters and node kinds are registered at run time, by name; no
  C++-only static registration (no static initialisers that a Python extension would duplicate:
  AGENTS.md "Never add a mutable inline variable to a header").
- Persistent widget tree: hover, probes and selection change no widget identity; adding or
  removing a node rebuilds the board's cards (the part 1 `simplify:` stands).
- AGENTS.md: TDD (failing test shown first), strong geometry types, `simplify:` comments on cuts,
  no nested namespace named bare `detail`, non-template code in `src/`. Zoom: every conversion
  between window and widget coordinates goes through `input::Placed::to_local/to_window` or
  `ui::place_of` — never a hand-written `(p − origin)/s`.
- Builds and tests on Coder through `/tmp/claude-6516/prism_remote.sh '<cmd>'`, one at a time,
  foreground; syntax checks without reflection and with clang via `bash ~/syntax_checks.sh`.
  Commit with explicit pathspecs; never push; examples never use the user's name.

## Review Focus

1. Removing a node the user selected while a wire from it is being hovered or dragged: no crash,
   the probe disappears. Test in Task 4.
2. A node dropped from the palette at zoom 0.5 lands with its top-left under the pointer (board
   point = window point mapped through the zoom). Test in Task 5.
3. A probe on a wire whose value changes while hovered follows the new value without moving the
   mouse. Test in Task 4.
4. Switching the theme (dark ↔ light) while the board is shown repaints the board, cards, grid and
   wires in the new look without a rebuild. Test in Task 2.
5. A port type with no registered colour or formatter (an app's own type) still gets a distinct
   wire colour and a probe showing its type name. Test in Task 1.

## File structure

- Modify `include/prism/nodes/port.hpp`, `src/nodes/port_type.cpp`: the registry gains a colour
  slot and a formatter per type.
- Modify `include/prism/ui/context.hpp` (Theme roles and both themes).
- Modify `src/app/node_board.cpp`, `include/prism/app/node_board.hpp`: drawing in the look, probes,
  drops of node kinds.
- Create `include/prism/nodes/node_kind.hpp` (`NodeKind`), modify `include/prism/nodes/graph.hpp`
  + `src/nodes/graph.cpp` (graph-owned nodes).
- Modify `include/prism/app/view_builder.hpp` + create `src/app/node_palette.cpp`
  (`vb.node_palette`).
- Gallery: `examples/widget_gallery/nodes_tab.*`, `patch.hpp` (more blocks), README; showcase
  `examples/showcase/showcase_node_board.cpp`; manual page `doc/widgets/…/node-board.md`.

---

### Task 1: Port types carry a wire colour and a probe text

**Files:** Modify `include/prism/nodes/port.hpp`, `src/nodes/port_type.cpp`. Test:
`tests/test_node_ports.cpp`.

**Interfaces — produces (namespace `prism::nodes`):**
```cpp
// The theme slot a type's wires are drawn in: built-ins fixed, others in registration order.
inline constexpr std::size_t wire_slots = 6;
std::size_t wire_slot(TypeKey);
// What a probe on a wire of this type shows; none registered: the type's name.
void set_formatter(TypeKey, std::function<std::string(const std::any&)>);
std::string format_value(TypeKey, const std::any&);
```
Built-in slots: waveform 0, number 1, boolean 2, integer 3, text 4; any other type
`(registration index) % wire_slots`, starting at 5. Built-in formatters: number `"{:.3g}"`,
integer, boolean `"true"/"false"`, text quoted and cut to 24 characters with `…`, waveform
`"{n} samples · peak {p:.2f}"`. Sugar: when `PortType<T>` has `static std::string format(const T&)`,
`type_key<T>()` registers it the first time (no static initialiser).

- [ ] **Step 1: failing tests** — the five built-in slots; an app type `struct Volts` with a
  `PortType` name and no formatter: slot 5, `format_value` gives `"Volts"`'s registered name;
  with a `format` member: its text; `format_value(number, 0.123456)` is `"0.123"`; a waveform of
  240 samples peaking at −0.83 gives `"240 samples · peak 0.83"`; an empty `std::any` gives the
  type name.
- [ ] **Step 2:** run red. **Step 3:** implement (the registry entry gains slot and formatter,
  under the existing mutex). **Step 4:** green; full suite; syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): port types carry a wire colour slot and a probe text`.

### Task 2: The look — theme roles, cards, board grid

**Files:** Modify `include/prism/ui/context.hpp`, `src/app/node_board.cpp` (+ its header if
needed). Test: `tests/test_node_board.cpp`, `tests/test_pixels.cpp`.

**Interfaces — produces (in `ui::Theme`):**
```cpp
Color node_board_bg, node_grid;   bool node_grid_dots;     // dots (look A) or lines (look B)
Color node_surface, node_border, node_border_selected, node_glow;
std::array<Color, nodes::wire_slots> node_wires;   // by wire_slot (Task 1)
Color node_probe_bg, node_probe_text;
struct NodeFamily { std::string name; Color color; };
std::vector<NodeFamily> node_families;   // source, filter, math, display; an app appends its own
Color node_family_default;               // a family not in the table
```
(Use `std::array` sized by a constant visible to `ui` without making `ui` include `nodes`: e.g.
`ui::node_wire_slots = 6`, with `nodes::wire_slots` defined equal to it — the builder's call,
stated in the report.)

Drawing (values from Global Constraints):
- The board background draws `node_board_bg` and the grid (dots or lines every 20 board units,
  moving with pan and zoom; skipped when the spacing on screen is under 8 px).
- A card: rounded rect radius 10 in `node_surface` with a 1 px `node_border`; its header row:
  background = family colour mixed 20 % over the surface, a dot of the family colour, the title,
  and the family name upper-cased on the right in the label mix. (The header is part 1's
  `node_header`; give it these looks through the card's frame and the header's draws — whichever
  keeps it a real widget row.)
- The selected node: border `node_border_selected` plus a 2 px ring in `node_glow`.
- `mix(a, b, t)` is a small pure function (in `core` colour helpers if one exists, else beside the
  theme), tested.

- [ ] **Step 1: failing tests** — `mix` values; the dark and light themes give the look A/B values
  above for every role (a table test); a board's snapshot: the background entry's first command
  is a `FilledRect` of `node_board_bg`; a card's header carries the family colour mix; an unknown
  family uses `node_family_default`. Review Focus 4: switching the tree's theme repaints the board
  in the new colours and keeps every card's widget ids. A pixel test: dark theme, a dot of the grid
  is drawn at a 20-unit step at zoom 1 and at 10 px at zoom 0.5.
- [ ] **Step 2:** red. **Step 3:** implement. **Step 4:** green; full suite; syntax checks;
  regenerate `doc/screenshots/node_board.svg` (the showcase's capture target builds it) and look at
  it (both themes if the showcase can capture light: `ninja` builds the SVG; convert with
  `rsvg-convert` locally to check).
- [ ] **Step 5: Commit** `feat(nodes): the board in the Instrument and Paper looks`.

### Task 3: Wires and ports in their type's colour

**Files:** `src/app/node_board.cpp`. Test: `tests/test_node_board.cpp`.

- Wires are drawn in `theme.node_wires[wire_slot(type)]`: waveform 3 px, others 2 px (× zoom,
  floors from part 2); the hovered or selected wire gets an under-stroke 9 px (× zoom) of its
  colour at 25 % alpha.
- Port dots: a rounded square 12 px (× zoom, floor 4 px), radius 3, in the type's colour, with a
  3 px ring of `node_board_bg` (the mockup's `box-shadow: 0 0 0 3px`).
- While a wire is dragged, ports that cannot take it are drawn at 35 % alpha (part 1 already
  knows which: `hint_targets`).
- The drag trail is drawn in the dragged type's colour.

- [ ] **Step 1: failing tests** — a waveform wire's polyline colour is slot 0 and thickness 3; a
  number wire's slot 1 and 2; a selected wire has the under-stroke; during a drag, an incompatible
  port's dot has alpha 35 % and a compatible one full.
- [ ] **Steps 2-4:** red, implement, green, full suite, syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): wires and ports in their type's colour`.

### Task 4: Probes

**Files:** `src/app/node_board.cpp`, `include/prism/app/node_board.hpp`. Test:
`tests/test_node_board.cpp`.

- The board background, on a mouse move with no button pressed, finds the wire under the pointer
  (`wire_at`, part 1); the board remembers `{edge, pointer}` and redraws. Hovering a card or
  leaving the board clears it.
- The bubble is drawn in `board_over` near the pointer (12 px right, 12 px up; kept inside the
  board), at natural size at any zoom: `format_value(type, output value)` in the theme's mono font
  at the theme's small size, padding 3×8, radius 10, background `node_probe_bg`, 1 px border in
  the wire's colour, text `node_probe_text`.
- While a probe is shown, a change of the probed output redraws the board (subscribe to the edge's
  output while probed; drop the subscription when the probe goes).
- Removing the probed wire or its node clears the probe.

- [ ] **Step 1: failing tests** — hovering a number wire shows `"0.500"`-style text from its
  output; Review Focus 3: changing the source slider while hovered changes the bubble's text with
  no mouse move; Review Focus 1: removing the source node while hovered clears the probe and draws
  no bubble; hovering a card shows no probe; at zoom 0.5 the bubble's text size is the natural one.
- [ ] **Steps 2-4:** red, implement, green, full suite, syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): hovering a wire shows its value`.

### Task 5: Graph-owned nodes, node kinds, the palette

**Files:** Modify `include/prism/nodes/graph.hpp`, `src/nodes/graph.cpp`; create
`include/prism/nodes/node_kind.hpp`, `src/app/node_palette.cpp`; modify
`include/prism/app/view_builder.hpp`, `src/app/node_board.cpp`. Test: `tests/test_node_graph.cpp`,
`tests/test_node_board.cpp`.

**Interfaces — produces:**
```cpp
// graph.hpp: a node the graph owns; destroyed when removed (after its wires) or with the graph.
template <class N, class... Args> NodeId Graph::emplace(NodeOptions, Args&&...);
// node_kind.hpp: a named recipe for one node, what a palette lists.
struct NodeKind {
    std::string title;
    std::string family;
    std::function<NodeId(Graph&, core::Point at)> add;   // adds one node at `at` (graph units)
};
// Sugar: a kind that emplaces N with `title`/`family`, removable.
template <class N> NodeKind kind_of(std::string title, std::string family);
// view_builder.hpp: a list of kinds. Dragging one onto a node_board of `graph` adds it where it
// is dropped; activating one (double-click, or Enter when focused) adds it at the visible
// board's top-left, cascading 24 units per add so repeated adds don't stack.
WidgetId node_palette(nodes::Graph& graph, std::span<const nodes::NodeKind> kinds);
```
- Each palette row shows the family's colour dot and the title (the theme's family colours).
- The board takes a dropped `NodeKind` (alongside port drags): the node's position is the drop
  point turned into graph units through the zoom (`board point = window point mapped by the
  board's Placed`, then `÷ zoom − pan`), so its card's top-left lands under the pointer.
- A graph-owned node is `removable = true` by default; Delete on the board removes it (part 1).
- The graph's destructor: owned nodes are destroyed after every edge is dropped; referenced
  (app-owned) nodes untouched, as today.

- [ ] **Step 1: failing tests** — `emplace` then `remove` destroys the node (a node type counting
  its destructions) after its wires are gone (its port's `on_change` subscriptions were dropped:
  no callback runs during or after); the graph's destructor destroys owned nodes once; Review
  Focus 2: dragging a palette row onto the board at zoom 0.5 adds one node whose card's top-left
  is under the drop point; activating a row twice adds two nodes 24 units apart; a palette row's
  family dot uses the family colour.
- [ ] **Steps 2-4:** red, implement, green, full suite, syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): graph-owned nodes, node kinds and a palette`.

### Task 6: The gallery's signal sandbox, the showcase, the manual page

**Files:** `examples/widget_gallery/nodes_tab.*`, `examples/widget_gallery/patch.hpp`,
`tests/test_patch.cpp`, `examples/widget_gallery/README.md`,
`examples/showcase/showcase_node_board.cpp`, a manual page under `doc/widgets/` (find the
family the board belongs with — "Data views" or its own; add it to that family's index and to
`doc/index.md`'s tree as the other widget pages are).

- The Nodes tab: a palette on the left (a fixed-width column), the board on the right, the zoom
  row above the board. Blocks: Sine, Square, Noise, Constant (a number), Gain (waveform × number
  input), Mixer, Low-pass, Scope, and a Number display (shows a number input). New DSP in
  `patch.hpp` (`square`, `gain`) with tests in `tests/test_patch.cpp`. The starting patch is the
  current one, built from graph-owned nodes, so it can be edited and deleted like any added one.
- The intro mentions dragging blocks from the palette and Delete to remove.
- The showcase uses the new look (and `display` instead of `sink`); its SVG is regenerated.
- The manual page: what a node board is, a complete example (a node struct with ports, a graph,
  `vb.node_board`, `vb.node_palette` with `kind_of`), editing gestures (move, wire, unplug,
  select + Delete, pan, Ctrl+wheel/pinch zoom, middle drag, Fit), probes and port types
  (registering a colour/formatter for an app type), families and theme roles; the screenshot.
  `sphinx-build doc <out>` with no warnings (the docs build runs locally with
  `doc/requirements.txt`; no C++ compiled).
- [ ] **Steps:** failing DSP tests first; implement; full suite; wasm gallery builds; docs build
  clean; commit `feat(gallery): a signal sandbox — add blocks from a palette, remove them`. The
  controller checks the tab in a real browser.
