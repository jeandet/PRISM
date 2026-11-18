# Node editor part 2: zoom — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The node board zooms from 25 % to 200 % around the pointer (Ctrl+wheel), pans with a
middle drag anywhere on it, and fits all nodes on request, while every widget inside a node stays
real: drawn at the scaled size and hit in its own coordinates.

**Architecture:** Layout keeps running at natural size. A board carries a scale; when flatten
writes the cards' subtree into the snapshot it maps it through one affine transform
`T(q) = O + s·(q − O)` (O = the board's top-left), draw commands included, and records each
geometry entry's scale so routing hands a widget `(p − origin) / s`. The layout writeback applies
the same transform, so `absolute_x/y` and `laid_out_at` are screen positions: everything the
board computes (wire ends, port hits, drag trails) stays in window coordinates and needs no zoom
code of its own.

**Tech Stack:** C++26, Meson, doctest; SDL3 backend (desktop and Emscripten).

**Spec:** docs/superpowers/specs/2026-11-17-node-editor-design.md (goal 5, "Zoom in the engine",
the performance target). Part 1, which this builds on:
docs/superpowers/plans/2026-11-17-node-editor-1-graph-board-wiring.md.

## Global Constraints

- Zoom range 25 % to 200 % (`min_zoom = 0.25f`, `max_zoom = 2.f`); Ctrl+wheel zooms around the
  pointer; middle-drag anywhere on the board pans, as does dragging the empty board; a "fit all"
  action.
- "Widgets inside zoomed nodes stay real and usable at any zoom: they are drawn scaled (text at
  the scaled size, never a scaled bitmap) and the pointer is turned back into their own
  coordinates" as `(p − origin) / s`.
- Anti-goal: "Zoom that scales a bitmap (blurry text) or that breaks clicking widgets inside nodes."
- Persistent widget tree: "moving a node, panning or zooming must not rebuild the node's widgets
  (it changes their placement only)."
- Performance target: "200 nodes and 300 wires pan and zoom smoothly (one frame per refresh)";
  measured with a headless count like `benchmarks/widget_scale.cpp`.
- No reflection required; works on desktop and in the browser.
- AGENTS.md: TDD (a failing test first, shown failing), pure logic tested headless, strong
  geometry types (`X`/`Y`/`DX`/`DY`/`Width`/`Height`, never a bare float for a coordinate),
  `simplify:` comments on deliberate cuts, no nested namespace named bare `detail`, non-template
  code in `src/`.
- Builds and tests run on Coder through `/tmp/claude-6516/prism_remote.sh '<cmd>'` (it syncs
  the tree and runs `<cmd>` in `~/PRSIM`): `ninja -C builddir` then `meson test -C builddir`.
  One build or test command at a time, in the foreground, never backgrounded. Syntax checks
  without reflection and with clang: `/tmp/claude-6516/prism_remote.sh 'bash ~/syntax_checks.sh <files…>'`.
- Commit with explicit pathspecs; never `git checkout` over work; never push.
- Examples never use the user's name (use "ada" for a person).

## Review Focus

1. A text field inside a node at 2× zoom: a click between two letters puts the caret there (local
   x divided by the scale). Test in Task 2.
2. Ctrl+wheel bursts and limits: zoom stops at 25 % and 200 %, and the board point under the
   pointer stays under it while zooming. Tests in Task 1 (math) and Task 4 (routing).
3. Wires and port dots follow the zoom: a wire still ends on its card's scaled edge, a drop on a
   scaled port still connects. Tests in Task 3.
4. A combo box inside a zoomed node: its list opens next to the scaled box and a click on an
   item selects it. Test in Task 3.
5. Zoom changes no widget identity: after zooming, the node's widget ids and a slider's value are
   unchanged (no rebuild). Test in Task 3.

## File structure

- Create `include/prism/render/view_transform.hpp`: `ViewTransform` (pure, header-only, tiny).
- Create `src/render/view_transform.cpp`: `scale_draw_list` (non-template).
- Create `include/prism/nodes/board_view.hpp` + `src/nodes/board_view.cpp`: `BoardView` and the
  zoom math (pure).
- Modify `include/prism/ui/layout.hpp` (Board flatten), `include/prism/render/scene_snapshot.hpp`
  (`scale`), `include/prism/input/hit_test.hpp` + `include/prism/input/input_event.hpp`
  (placement, localize), `include/prism/app/event_routing.hpp` (every `localize_mouse` call).
- Modify `include/prism/app/layout_writeback.hpp`, `include/prism/ui/widget_node.hpp`
  (`view_scale`), `include/prism/app/widget_tree.hpp` (`laid_out_at`), `include/prism/ui/node.hpp`
  (`BoardDraw` signature, `board_scale`, `claims`), `include/prism/app/widget_tree_layout.hpp`.
- Modify `include/prism/nodes/graph.hpp` + `src/nodes/graph.cpp` (view state),
  `src/app/node_board.cpp` + `include/prism/app/node_board.hpp`.
- Modify `src/backends/software_backend.cpp` (wheel modifiers).
- Gallery: `examples/widget_gallery/nodes_tab.*`. Benchmark: `benchmarks/node_board_scale.cpp`.
- Tests: `tests/test_view_transform.cpp`, `tests/test_board_view.cpp` (new), plus
  `tests/test_board_layout.cpp`, `tests/test_node_board.cpp`, `tests/test_pixels.cpp`.

---

### Task 1: The pure math — view transform, draw scaling, board view on the graph

**Files:**
- Create: `include/prism/render/view_transform.hpp`, `src/render/view_transform.cpp`
- Create: `include/prism/nodes/board_view.hpp`, `src/nodes/board_view.cpp`
- Modify: `include/prism/nodes/graph.hpp`, `src/nodes/graph.cpp`, `src/app/node_board.cpp`
  (callers of `pan()` only)
- Test: `tests/test_node_graph.cpp`; `tests/test_view_transform.cpp`, `tests/test_board_view.cpp` (register both in
  `tests/meson.build` `headless_tests`; add the two `.cpp` to libprism's sources where
  `src/nodes/graph.cpp` is listed)

**Interfaces:**
- Produces (namespace `prism::render`):
  ```cpp
  // A zoom of a subtree about a fixed point: q -> origin + scale·(q − origin).
  struct ViewTransform {
      core::Point origin{};
      float scale = 1.f;
      core::Point apply(core::Point q) const;
      core::Rect apply(core::Rect r) const;      // origin mapped, extent × scale
      core::Point invert(core::Point p) const;   // apply's inverse
      bool identity() const { return scale == 1.f; }
  };
  // Every command of `dl` through `t`: points, rect origins and extents, circle radii, line and
  // outline thicknesses (0 = filled stays 0), rounded-rect radii, text size and origin, image
  // rects, clip rects.
  void scale_draw_list(DrawList& dl, const ViewTransform& t);
  ```
- Produces (namespace `prism::nodes`):
  ```cpp
  // How the board shows the graph: a node at `position` lies at board origin + zoom·(position + pan).
  struct BoardView {
      core::Point pan{};
      float zoom = 1.f;
      bool operator==(const BoardView&) const = default;
  };
  inline constexpr float min_zoom = 0.25f;
  inline constexpr float max_zoom = 2.f;
  inline constexpr float zoom_per_notch = 1.1f;
  float clamp_zoom(float z);
  // The view at `zoom` (clamped) that keeps the board point under `pointer` (window offset from
  // the board's top-left) where it is.
  BoardView zoomed_about(BoardView v, core::Offset pointer, float zoom);
  // The zoom after `notches` wheel notches (fractional from touchpads; > 0 zooms in): zoom × 1.1^notches, clamped.
  float wheel_zoom(float zoom, float notches);
  // The view showing every rect (node cards, at natural size, in graph positions) inside a
  // board of `size`, `margin` px clear on each side; zoom clamped and at most 1. Empty: {}.
  BoardView fit_all(std::span<const core::Rect> cards, core::Size size, float margin = 24.f);
  ```
- Produces on `Graph`: `BoardView view() const`; `void set_view(BoardView)` (zoom clamped; bumps
  `layout` only when the view changed); `pan_by(DX, DY)` stays and moves `view().pan` in board
  units; `pan()` is removed (its callers in node_board.cpp read `view().pan`; zoom is not used
  there yet). The pan's private member becomes a `BoardView`.

- [ ] **Step 1: Write the failing tests.** `tests/test_view_transform.cpp`:
  - `apply` of `{origin {100,50}, scale 0.5}` maps `{300,250}` to `{200,150}`; a rect
    `{{300,250},{80,40}}` to `{{200,150},{40,20}}`; `invert(apply(p)) == p` for `{17,-3}`.
  - `scale_draw_list` with that transform: a `FilledRect`, a `Line` of thickness 2 (→ 1), a
    `Circle` radius 10 filled (→ radius 5, thickness stays 0), a `TextCmd` size 14 (→ 7, origin
    mapped), a `ClipPush` rect, a `Polyline`'s points, a `RoundedRect` radius 6 (→ 3) all come
    out as expected; `ClipPop` untouched.
  `tests/test_board_view.cpp`:
  - `clamp_zoom(0.1f) == 0.25f`, `clamp_zoom(5.f) == 2.f`.
  - `wheel_zoom(1.f, 1.f) == Approx(1.1f)`; `wheel_zoom(1.9f, 3.f) == 2.f`; `wheel_zoom(0.3f, -5.f) == 0.25f`.
  - `zoomed_about`: with `v = {pan {10,0}, zoom 1}` and pointer `{110, 40}`, the board point
    under the pointer is `pointer/zoom − pan = {100, 40}`; after `zoomed_about(v, pointer, 2.f)`
    the same formula with the new view gives `{100, 40}` again and `zoom == 2`. A zoom of 9
    clamps to 2 and still keeps the point.
  - `fit_all` of cards `{{0,0},{200,100}}` and `{{600,300},{200,100}}` in a board `{500, 300}`:
    every card mapped by the result (`zoom·(p + pan)`) lies within `[24, 476] × [24, 276]`; the
    zoom is the largest that does (`min(452/800, 252/400)`). One small card in a big board:
    zoom 1, centred. No cards: `BoardView{}`.
  `tests/test_node_graph.cpp`: `set_view({{5,5}, 9.f})` stores zoom 2 and bumps `layout` once;
  setting the same view again does not bump it; `pan_by(DX{10}, DY{0})` adds 10 to
  `view().pan.x`.
- [ ] **Step 2:** Build and run: `/tmp/claude-6516/prism_remote.sh 'ninja -C builddir && meson test -C builddir view_transform board_view'`. Expected: fail (missing symbols or wrong values — stubs returning `{}` are fine for the red run).
- [ ] **Step 3:** Implement. `scale_draw_list` visits each command like
  `layout_detail::translate_cmd` (`include/prism/ui/layout.hpp`) does, with the scale applied to
  sizes as listed.
- [ ] **Step 4:** Run the two tests: pass. Run the full suite: pass.
- [ ] **Step 5: Commit** `feat(render,nodes): view transform and board zoom math`.

### Task 2: The engine — a board's cards flattened through its zoom, hit in their own coordinates

**Files:**
- Modify: `include/prism/ui/node.hpp` (`Node::board_scale`), `include/prism/app/widget_tree_layout.hpp`
  (copy it as `board_places` is), `src/app/node_board.cpp` (`b.board_scale = [&graph] { return graph.view().zoom; }`)
- Modify: `include/prism/ui/layout.hpp` (`LayoutNode::board_scale`, `flatten_board`),
  `include/prism/render/scene_snapshot.hpp`, `include/prism/input/hit_test.hpp`,
  `include/prism/input/input_event.hpp`, `include/prism/app/event_routing.hpp`
- Test: `tests/test_board_layout.cpp`, `tests/test_node_board.cpp` (or a new
  `tests/test_zoomed_input.cpp` if cleaner)

**Interfaces:**
- Consumes: `render::ViewTransform`, `render::scale_draw_list` (Task 1).
- Produces:
  - `LayoutNode::board_scale` (`float`, default 1): Kind::Board only; read at each layout from
    `ui::Node::board_scale` (`std::function<float()>`), which the node board sets from the
    graph's zoom. (Until Task 3, wires and `laid_out_at` are still unscaled: expected.)
  - `SceneSnapshot::scale` — `std::vector<float>` parallel to `geometry` (each entry's scale; 1
    outside zoomed boards). Empty in a snapshot built by hand: read as 1. `push_geometry` always
    pushes, so a flattened snapshot has it full.
  - `input::Placed { Rect rect; float scale = 1.f; }` and
    `std::optional<Placed> find_widget_place(const SceneSnapshot&, WidgetId)` (geometry entries
    carry their scale; overlay entries scale 1).
  - `input::localize_mouse(const InputEvent&, Rect widget_rect, float scale = 1.f)`: position
    becomes `(p − origin) / scale`; wheel deltas are not scaled.
- Behavior of `flatten_board` when `board_scale != 1`, with `T = {board.allocated.origin, s}`:
  the background (child 0) and `board_under`/`board_over` are not transformed; every entry the
  cards (children 1…) append — geometry, draw lists, `scrollbar_geometry`, `overlay_geometry`,
  `overlay_placed`, overlay commands — is mapped through `T`, and `visible` is recomputed as the
  board's visible area narrowed to the mapped rect. A card is skipped when its mapped rect does
  not show. Card entries are fresh (their clip is set, so the per-widget draw cache never shares
  them): map copies, never a shared entry.
- `simplify:` comments to leave: text is drawn at `size × s` from the natural layout (no
  re-measure; re-measure at the zoom level if overflow shows); a board inside a zoomed board's
  node is not composed (assert it, or document it as unsupported).

- [ ] **Step 1: Write the failing tests.**
  - Layout (`test_board_layout.cpp`): a board at origin `{10,20}`, `board_scale = 0.5`, a card
    at board point `{200,100}` holding a leaf `{0,0,80,40}`: after measure, arrange at
    `{{10,20},{800,600}}` and flatten, the leaf's geometry is `{{110,70},{40,20}}`, its scale
    entry is `0.5`, its draw list's `FilledRect` is `{{110,70},{40,20}}`, and the background's
    entry keeps scale 1 and the board's full rect. A card that would lie wholly outside the board
    once scaled (but inside it unscaled) is not flattened.
  - Routing (`test_node_board.cpp`, through a real `WidgetTree` and `route_event` as the other
    tests there do; set the zoom with `graph.set_view`):
    1. A slider in a node at zoom 0.5: pressing on the thumb's drawn centre and moving right by
       `w/2` screen pixels (w = the track's natural width × 0.5) moves the value by about half
       its range, not the whole of it.
    2. (Review Focus 1) A text field in a node at zoom 2 holding "abcdef": a click at the screen
       x of the boundary between "c" and "d" puts the caret at 3.
    3. At zoom 1 every existing test still passes unchanged.
  - Pixel (`tests/test_pixels.cpp`, the spec's "pixel test"): a node with a slider at zoom 0.5:
    the thumb's accent-colored pixels lie inside the mapped thumb rect, and a press at their
    centre followed by a drag moves the value (the drawn thumb and the hit thumb coincide).
    Show it fails with the scaling of draw lists removed.
- [ ] **Step 2:** Run them red (`meson test -C builddir board_layout node_board pixels`).
- [ ] **Step 3:** Implement: `board_scale` on `LayoutNode`; flatten the cards, then map the
  entries appended since (a stateless helper in `layout_detail`, e.g.
  `zoom_entries(SceneSnapshot&, Marks from, const ViewTransform&, Rect visible)`); add `scale`
  to the snapshot; `find_widget_place` + scaled `localize_mouse`; route every
  `rect ? localize_mouse(ev, *rect) : ev` in `event_routing.hpp` (and the drop place there)
  through one helper that uses the placement's scale.
- [ ] **Step 4:** Tests green; full suite green; syntax checks on the touched headers' users
  (`src/app/widget_tree.cpp`, `tests/test_board_layout.cpp`).
- [ ] **Step 5: Commit** `feat(ui): a board's cards are drawn and hit through its zoom`.

### Task 3: The board zooms — view state on the graph, screen positions in the writeback

**Files:**
- Modify: `include/prism/nodes/graph.hpp`, `src/nodes/graph.cpp` (`request_fit`)
- Modify: `include/prism/app/layout_writeback.hpp`, `include/prism/ui/widget_node.hpp`,
  `include/prism/app/widget_tree.hpp`, `include/prism/ui/node.hpp`,
  `include/prism/app/widget_tree_layout.hpp`, `include/prism/ui/layout.hpp`
  (`flatten_board_draw`)
- Modify: `src/app/node_board.cpp`, `include/prism/app/node_board.hpp`
- Test: `tests/test_node_graph.cpp` (request_fit), `tests/test_node_board.cpp`

**Interfaces:**
- Consumes: `nodes::BoardView`, `zoomed_about`, `fit_all`, `clamp_zoom` (Task 1);
  `LayoutNode::board_scale`, `find_widget_place` (Task 2).
- Produces:
  - `Graph::request_fit()` — the board fits all cards at its next layout (a flag the board
    clears) and bumps `layout`.
  - `BoardDraw` becomes `std::function<void(DrawList&, const BoardRectOf&, float scale)>`;
    `flatten_board_draw` hands it the cards' rects mapped through the zoom (board-relative), so
    wires are drawn in screen space; wire thickness and dot radius are multiplied by `scale`
    with a floor of 1 px (thickness) and 3 px (radius).
  - `WidgetNode::view_scale` (`float`, 1 outside zoomed boards). The writeback
    (`update_canvas_bounds`) carries the board's `ViewTransform` down the cards' subtrees:
    `absolute_x/y` become `T(natural − scrolled)` and `view_scale = s`; `arranged_extent` stays
    natural (widgets record at natural size). `WidgetTree::laid_out_at` returns the screen rect
    (`absolute`, `arranged_extent × view_scale`).
  - `board_coords` (node_board.cpp) — `to_window(n, local) = absolute + local × view_scale`,
    `to_local` its inverse; `on_board` and `dragged` use `view().pan`/`zoom`; the background
    drag pans by `d / zoom`. Nothing else in node_board needs zoom code.
- `simplify:` to leave in the writeback: a board nested in a zoomed board's node is not
  composed.

- [ ] **Step 1: Write the failing tests.**
  - Board, at `set_view({{0,0}, 0.5f})`:
    1. A card at graph position `{200,100}` on a board at window `{0,0}` has
       `tree.laid_out_at(card)->origin == {100,50}` and extent half its natural extent.
    2. (Review Focus 3) The wire from an output to an input is drawn from the output card's
       mapped right edge to the input card's mapped left edge, level with the mapped port rows;
       dragging from an output's dot (its screen position) to an input's dot connects them.
    3. (Review Focus 5) Zooming from 1 to 0.5 and back keeps every card's widget ids and a
       slider's value; no build of the board's content runs (count `build_content` calls as the
       part 1 tests do for moves).
    4. Dragging the empty board by `{40,0}` screen px at zoom 0.5 moves `view().pan.x` by 80.
    5. A node header drag at zoom 0.5 by 40 screen px moves the node's position by 80.
    6. `request_fit()` with two cards far apart: after the next publish every card's
       `laid_out_at` rect lies inside the board's rect.
    7. (Review Focus 4) A combo box (`ui::Dropdown`/combo widget the gallery uses) in a node at
       zoom 0.5: a click on it opens its list next to the box's screen rect, and a click on the
       second item selects it. If the open list's placement needs a change in `popup_place` or
       `flatten_overlays`, make it here, with this test as the reproducer.
- [ ] **Step 2:** Run red.
- [ ] **Step 3:** Implement as the Interfaces say. The fit uses the cards' natural extents
  (`arranged_extent`) and the board's laid-out size, computed in the board's places hook when the
  flag is set.
- [ ] **Step 4:** Tests green; full suite green; syntax checks on `src/app/node_board.cpp`,
  `src/nodes/graph.cpp`, `src/app/widget_tree.cpp`.
- [ ] **Step 5: Commit** `feat(nodes): the board zooms and fits; wires and ports follow`.

### Task 4: Input — Ctrl+wheel zooms about the pointer, a middle drag pans anywhere

**Files:**
- Modify: `include/prism/input/input_event.hpp` (`MouseScroll::mods`),
  `src/backends/software_backend.cpp` (fill it from SDL's modifier state)
- Modify: `include/prism/ui/node.hpp`, `include/prism/ui/widget_node.hpp`,
  `include/prism/app/event_routing.hpp` (claims)
- Modify: `src/app/node_board.cpp`
- Test: `tests/test_node_board.cpp`, `tests/test_event_routing.cpp` (or the routing test file
  that exists for wheel routing)

**Interfaces:**
- Consumes: `Graph::view/set_view`, `nodes::wheel_zoom`, `zoomed_about` (Tasks 1, 3).
- Produces:
  - `MouseScroll { Point position; DX dx; DY dy; uint16_t mods = 0; }`.
  - `WidgetNode::claims` — `std::function<std::optional<WidgetId>(const InputEvent&)>`, set on a
    container: for a `MouseScroll` or a middle `MouseButton` press hitting anything inside it, the
    router asks the hit widget's ancestors, nearest first; the first that returns an id gets the
    event instead (localized to that widget's placement), and for a press, the capture. Nothing
    claims: routing is as today.
  - The board claims Ctrl+wheel (`mods & mods::ctrl`) and middle presses for its background:
    the background zooms with `set_view(zoomed_about(view, pointer − board origin,
    wheel_zoom(zoom, dy)))`, and a middle drag pans exactly as a left drag on the empty board.
- `simplify:` to leave: desktop pinch gestures (SDL pinch events) are not read; the browser
  reports a trackpad pinch as Ctrl+wheel, which this handles.

- [ ] **Step 1: Write the failing tests.**
  - A Ctrl+wheel (`dy = 1`, mods ctrl) over a card's slider zooms the board to 1.1 and leaves the
    slider's value alone; the board point under the pointer stays under it (compare
    `laid_out_at` of the card before/after against the formula). Without Ctrl, the wheel does
    what it does today.
  - (Review Focus 2) Ten Ctrl+wheel notches up end at zoom 2; thirty down end at 0.25.
  - A middle press on a card's slider and a move by `{30,0}` pans the board (view pan +30/zoom)
    and leaves the slider's value; a left press there still drags the slider.
  - The SDL backend fills `mods` on wheel events (a small check where the backend is testable,
    otherwise read the code in review).
- [ ] **Step 2:** Run red.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests green; full suite green; syntax checks on the touched sources.
- [ ] **Step 5: Commit** `feat(nodes): Ctrl+wheel zooms the board, a middle drag pans it`.

### Task 5: Gallery, speed, browser

**Files:**
- Modify: `examples/widget_gallery/nodes_tab.hpp`, `examples/widget_gallery/nodes_tab.cpp`,
  `examples/widget_gallery/README.md`
- Create: `benchmarks/node_board_scale.cpp` (register in `benchmarks/meson.build` as
  `widget_scale` is)

**Interfaces:**
- Consumes: `Graph::request_fit`, `Graph::view` (Task 3).

- [ ] **Step 1:** Nodes tab: a row under the intro with a "Fit" button (`graph.request_fit()`),
  a "100 %" button (`set_view({view().pan, 1.f})`), and a label showing the zoom
  (`std::format("{:.0f} %", zoom × 100)`, a `Derived` of `graph.layout`). The intro text
  mentions Ctrl+wheel and the middle drag.
- [ ] **Step 2:** `benchmarks/node_board_scale.cpp`: a graph of 200 nodes (a slider and two
  ports each) and 300 wires, on a 1600×1000 headless tree; report the mean publish time over
  50 publishes for: a pan step, a zoom step, a slider change, at zoom 1 and 0.5. Print one line
  per case. Record the numbers in the commit message; if a case exceeds 16 ms, note it in the
  plan's ledger as a finding (do not optimize in this task).
- [ ] **Step 3:** Full suite green; build `widget_gallery` for the web on Coder
  (`source ~/emsdk/emsdk_env.sh; unset CC CXX; ninja -C builddir-wasm-release examples/widget_gallery/widget_gallery.html`),
  and in a headless Chromium (real time) check: Ctrl+wheel over the board zooms around the
  pointer and the page itself does not zoom; a slider in a node at 50 % drags by its thumb; Fit
  shows every node. Screenshot at 50 %.
- [ ] **Step 4: Commit** `feat(gallery): the Nodes tab zooms; a board benchmark`.
