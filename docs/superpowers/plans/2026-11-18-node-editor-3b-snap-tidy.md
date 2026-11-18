# Node editor part 3b: magnetism and tidy — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dragging a node snaps it to its neighbours (edges and centres line up, a wired input
levels with its output so the wire runs straight) with guide lines showing why, and a "tidy"
request lays the whole graph out in columns by data flow, gliding the nodes there.

**Architecture:** Two pure, headless-tested functions in `prism::nodes` do the geometry: `snap`
(where a dragged card settles and which guides to draw) and `tidy_layout` (layered layout: column
by longest path from the sources, order in a column by barycentre sweeps). The board calls them:
the header drag runs every move through `snap`, and `Graph::request_tidy()` is applied in the
board's places hook (where card sizes are known, like `request_fit`), then glides the nodes to
their targets over a few frames.

**Tech Stack:** C++26, Meson, doctest.

**Spec:** docs/superpowers/specs/2026-11-17-node-editor-design.md (non-goals listed "wire routing";
snapping and auto-layout were not covered). The user asked on 2026-11-18: "some dynamic helper to
position nodes, like centering rules and a bit of magnetism", and agreed to "the three snap rules
with guide lines, and a Tidy button running a simple layered layout, animated" (grid snap was
offered; this plan leaves it out — see Task 1).

## Global Constraints

- Snap threshold 6 px on screen (graph units: `6 / zoom`); holding Alt while dragging moves
  freely (no snap, no guides).
- Alignment candidates: the dragged card's left, centre, right against every other card's left,
  centre, right (x); top, middle, bottom against top, middle, bottom (y). The nearest candidate
  within the threshold wins per axis; on a tie, a wire straightening wins.
- Wire straightening: for each wired input of the dragged node, the y shift that puts its port
  level with the output feeding it; for each wired output, with the input it feeds. It is a y
  candidate like the others.
- Guides: one per snapped axis, drawn while the drag lasts, as a 1 px line in the theme's accent
  at 60 % alpha, spanning both cards (an alignment) or the wire (straightening); none after the
  release.
- Tidy: columns 80 graph units apart, cards in a column 40 apart; the result keeps the graph's
  current top-left (the bounding box's top-left does not jump); nodes with no wires go to the
  first column, after the others; the glide takes 200 ms with an ease-out (cubic), and a node the
  user grabs during the glide stops gliding.
- Zoom: every window↔widget conversion through `input::Placed`/`ui::place_of`; graph↔board
  through `nodes::graph_point`. No hand-written `(p − origin)/s`.
- Persistent widget tree: snapping and tidy move nodes only (no rebuild of cards).
- AGENTS.md: TDD (failing test shown first), strong geometry types, `simplify:` on cuts, no bare
  `detail`, non-template code in `src/`. Builds on Coder via `/tmp/claude-6516/prism_remote.sh`,
  one at a time; syntax checks via `bash ~/syntax_checks.sh`; commit with explicit pathspecs;
  never push.

## Review Focus

1. Dragging a node across several neighbours: it must not "stick" — once the pointer is more
   than the threshold past a snap, the card follows the pointer again (snap is computed from the
   unsnapped position each move, never from the last snapped one). Test in Task 2.
2. At zoom 0.25 the snap distance is still 6 screen px (24 graph units), not 6 graph units.
   Test in Task 2.
3. Tidy on a graph with a node added during the glide, or removed: no crash; the added node is
   placed by the next tidy only. Test in Task 4.
4. Tidy on a diamond (A→B, A→C, B→D, C→D) and on a chain with a long skip edge (A→B→C, A→C):
   C is in column 2, not 1. Test in Task 3.
5. A drag with Alt held never snaps and draws no guide. Test in Task 2.

---

### Task 1: `snap` — where a dragged card settles (pure)

**Files:** Create `include/prism/nodes/snap.hpp`, `src/nodes/snap.cpp`; test
`tests/test_node_snap.cpp` (register in `tests/meson.build`, source in `src/meson.build`).

**Interfaces — produces (namespace `prism::nodes`):**
```cpp
// A line the board draws while a snap holds, in graph units.
struct Guide { core::Point from, to; };
// A wire end that would level with the dragged card: the port's y on the dragged card relative
// to the card's top, and the y of the other end (graph units).
struct LevelWith { core::DY port_from_top; core::Y other_end; };
struct Snapped { core::Point at; std::vector<Guide> guides; };
// `at`: where the pointer puts the card's top-left (unsnapped); `size`: its size; `others`: the
// other cards; `wires`: its wired ports; `reach`: the snap distance in graph units.
Snapped snap(core::Point at, core::Size size, std::span<const core::Rect> others,
             std::span<const LevelWith> wires, float reach);
```
- [ ] **Step 1: failing tests** — no other card: `at` unchanged, no guides; left edges 4 apart
  with reach 6 snap (guide spans both cards vertically at that x); 7 apart do not; a centre-x
  match; a bottom-to-bottom match; x and y snap independently (two guides); a wire level 3 away
  and an alignment 3 away: the wire wins; the nearest of two candidates wins; Review Focus 1 at
  the pure level: snapping depends only on `at` (the same `at` gives the same result whatever
  was snapped before).
- [ ] **Steps 2-4:** red, implement, green; full suite; syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): where a dragged card snaps, and the guides that say why`.
→ skipped: grid snap; add when an app asks (it would be one more candidate per axis, lowest priority).

### Task 2: The board snaps a dragged node and draws its guides

**Files:** `src/app/node_board.cpp` (NodeGrip), `src/app/node_board_paint.cpp` (guides),
`src/app/node_board_parts.hpp` / `include/prism/app/node_board.hpp` (the guides being shown).
Test `tests/test_node_board.cpp`.

- The grip's move computes the unsnapped position as today, then `snap` with the other cards'
  rects in graph units (their positions and natural sizes, `arranged_extent`), the node's wired
  ports as `LevelWith` (port row centre relative to the card top; the other end's y from its
  card's position and row), `reach = 6 / zoom`; moves the node to the snapped point; stores the
  guides for `board_over`. Alt held (the move event's or the press's mods): no snap, no guides.
  Release clears the guides.
- Guides are drawn mapped to the board (graph → board through the view), 1 px, accent at 60 %.

- [ ] **Step 1: failing tests** — dragging a node so its left edge comes 4 screen px from
  another's at zoom 1: its position snaps and one guide is drawn; Review Focus 2: at zoom 0.25,
  5 screen px (20 graph units) snaps, 7 screen px does not; Review Focus 1: continuing the drag
  10 px further, the node follows the pointer again; a wired input's port levels with its
  source's output (the wire's ends have the same y); Review Focus 5: with Alt, no snap and no
  guide; after the release no guide is drawn.
- [ ] **Steps 2-4:** red, implement, green; full suite; syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): a dragged node snaps to its neighbours and straightens its wires`.

### Task 3: `tidy_layout` — a layered layout (pure)

**Files:** Create `include/prism/nodes/tidy.hpp`, `src/nodes/tidy.cpp`; test
`tests/test_node_tidy.cpp`.

**Interfaces — produces:**
```cpp
struct TidySpacing { float column_gap = 80.f; float row_gap = 40.f; };
// Where each node goes: columns by the longest path from a source, ordered in a column to cross
// fewer wires, anchored at the current bounding box's top-left. `sizes`: each node's card size.
std::map<NodeId, core::Point> tidy_layout(const Graph& graph,
                                          const std::map<NodeId, core::Size>& sizes,
                                          TidySpacing spacing = {});
```
Algorithm (state it in a comment with a link to "Sugiyama layered graph drawing"): Kahn's
topological order over the graph's edges; column = longest path from any source; nodes without
wires last in column 0; order within each column by the median/barycentre of their neighbours'
order in the previous column, sweeping left→right then right→left, 4 sweeps; x of a column = the
previous column's x + its widest card + column_gap; y stacks with row_gap; then translate so the
result's bounding-box top-left equals the input positions' bounding-box top-left.

- [ ] **Step 1: failing tests** — a chain A→B→C: three columns, x strictly increasing by width
  + 80; Review Focus 4: the diamond (D in column 2, B and C in column 1) and the skip edge (C in
  column 2); two parallel chains whose wires would cross in the input order end up uncrossed
  (the order in column 1 follows column 0); an isolated node lands in column 0 after the wired
  ones; the top-left is kept; an empty graph gives an empty map.
- [ ] **Steps 2-4:** red, implement, green; full suite; syntax checks.
- [ ] **Step 5: Commit** `feat(nodes): a layered layout of the graph`.

### Task 4: Tidy on the board, gliding; the gallery button; the manual

**Files:** `include/prism/nodes/graph.hpp`, `src/nodes/graph.cpp` (`request_tidy`),
`src/app/node_board.cpp` (apply + glide), gallery `examples/widget_gallery/nodes_tab.*`,
`doc/widgets/data/node-board.md`. Test `tests/test_node_board.cpp`, `tests/test_node_graph.cpp`.

- `Graph::request_tidy()` — like `request_fit`: a flag, applied by the board when it knows its
  cards' sizes (in the places hook, next to the fit); bumps `layout`.
- The board computes `tidy_layout` and glides each node from where it is to its target over
  200 ms (ease-out cubic), one `graph.move` per frame, using the tree's existing timed-redraw path
  (see `WidgetTree::next_wake` / `tick` in src/app/widget_tree.cpp — the board must not need an
  app clock in its API). A node grabbed during the glide stops gliding; a node removed during it
  is skipped; a node added during it is not moved.
- Gallery: a "Tidy" button beside Fit. The manual page gets a short "Arranging nodes" section:
  snapping (Alt to move freely), tidy.

- [ ] **Step 1: failing tests** — `request_tidy` sets the flag and bumps `layout`; after a tidy
  and enough frames (advance the tree's clock past 200 ms), every node is at its `tidy_layout`
  target; mid-glide (100 ms) a node is between start and target; grabbing a node mid-glide
  leaves it where the drag puts it; Review Focus 3: removing a node mid-glide, then frames: no
  crash, the others arrive.
- [ ] **Steps 2-4:** red, implement, green; full suite; syntax checks; wasm gallery builds; docs
  build clean (`sphinx-build -q doc <out>` locally).
- [ ] **Step 5: Commit** `feat(nodes): tidy lays the graph out in columns, gliding there`. The
  controller checks snapping and Tidy in a real browser.
