# Allocation-free frames

**Status:** Proposal — for review before code.

## Why

PRISM runs on a microcontroller now (the STM32H7B3I-DK port, in the separate prism-stm32
repository). The people it should reach there — embedded and real-time DSP developers — expect
a program that allocates at startup and never after. Per-frame `malloc` is a red flag to them,
for good reasons: its timing is not bounded, the heap fragments over days of uptime, and an
out-of-memory failure shows up at runtime on the device instead of at build time.

The goal: **once startup is done, building and drawing a frame allocates nothing**, on every
platform, with one code path. An embedded build can then enforce it.

## Where we are

Measured on the prism-stm32 audio scope (title, three controls, two live plots, a label), on
the host with `host/build/alloc_sites`, which records the stack of every `operator new` during
one warmed-up `build_snapshot()` and groups them by the innermost PRISM function:

| Allocations | Function | What allocates |
|---|---|---|
| 28 | `layout_detail::flatten_overlays` | the new snapshot's vectors (geometry, draw lists, z-order, overlay geometry), grown from empty each build |
| 20 | `widget_detail::refresh_dirty` | re-recording dirty widgets: commands' own payloads (`PolylineCmd::points`, `TextCmd::text`), and `PlotModel::canvas`' two temporary `Field<AxisRange>` (each with a `SenderHub` state) |
| 10 | `plot::compute_ticks` | tick vectors and label strings, rebuilt each redraw |
| 6 | `layout_detail::flatten_node_draws` | `make_shared<DrawList>` for each dirty widget's translated, clipped copy |
| 5 | other | `build_snapshot` itself (the snapshot), `build_layout` |

**69 per frame.** On the board (newlib, every `malloc` counted) the timing line read 300 per drawn
frame until the prism-stm32 runtime stopped building snapshots faster than it drew them; it now
reads 65. prism-stm32's own rasterizer allocated 1660 per frame (a scratch vector per filled
quad) and now allocates none.

**After part 1** (phases 1, 2, 3 and 5; plan `docs/superpowers/plans/2026-11-20-allocation-free-frames-1.md`):
19 per frame on the host, 20 on the board. A publish with nothing changed allocates nothing.
What is left:

| Allocations | Function | What allocates |
|---|---|---|
| 8 + 6 | `flatten_overlays`, `flatten_node_draws` | each dirty widget's new translated, clipped copy and its commands' payloads (phase 4) |
| 4 | `ObservableValue<CursorState>::get()` | reading the plot's cursor field allocates: to investigate (read tracking?) |
| 1 | `build_layout` | a dirty subtree's layout node |

Part 1 also deviated from the design in one place: the snapshot pool lives in the runtime that
owns the handoff (prism-stm32 keeps four slots), and PRISM provides
`WidgetTree::build_snapshot(SceneSnapshot&, …)`, which rebuilds into an existing snapshot. The
`SceneSnapshot` contract and the `unique_ptr` overload are unchanged; `model_app` can adopt slots
the same way later.

## Prior art

- **Everything static, sized at compile time** — TouchGFX (screens are classes, widgets their
  members; one static `FrontendHeap` sized for the largest screen), microui (one fixed command
  array, overflow is an assert). Zero allocation, but no runtime-built widget tree.
- **Own heap in a static pool** — LVGL (TLSF over `LV_MEM_SIZE`), emWin, Embedded Wizard.
  Bounded memory, but they still allocate per frame; LVGL is criticized for it.
- **Allocate at init, never in the loop** — MISRA C++, AUTOSAR and JSF rules; FreeRTOS/Zephyr
  static kernel objects; real-time audio (JUCE allocates in `prepareToPlay`, never on the audio
  thread). ETL supplies fixed-capacity containers for this style.
- **Reuse so the steady state does not allocate** — Dear ImGui (vectors cleared, capacity kept);
  `std::pmr` arenas reset per frame (game engines).

PRISM's persistent widget tree rules out the first group. It takes the third group's rule and
the fourth group's means.

## Design

One code path for every build. Not a flag that swaps `std::vector` for fixed-capacity
containers: that doubles the test matrix and needs capacities PRISM cannot know (widget counts
are the app's). Startup allocation (building the tree, fonts, buffers) stays allowed.

### 1. Guard and regression tests (first)

- **Embedded guard** (prism-stm32, build option): the allocation counter becomes a guard. The
  app declares startup done; any later `malloc` traps with the caller's address.
- **PRISM tests:** a new kind of test builds a widget's snapshot twice and checks that the second
  build allocates nothing (an allocation-counting `operator new` in that test executable). It
  starts with the scope's widgets (plot, label, checkbox, slider) and spreads to others.

### 2. Plot quick wins (~14)

- `PlotModel::canvas` passes the resolved axis ranges as plain values to `render_plot_panel`,
  not as temporary `Field`s.
- `compute_ticks` fills a fixed array (a plot shows at most about 12 ticks per axis) with a
  count; labels short enough for the string's small buffer, or written into a fixed char buffer.

### 3. Snapshot pool (~34)

`WidgetTree` keeps a pool of snapshots: at most three in flight (being built, waiting, on
screen). A snapshot goes back to the pool when its last holder releases it — a custom deleter on
its `shared_ptr` — and is cleared with its vectors' capacity kept. The contract does not change:
nobody writes a snapshot while anyone holds it. The pool's own control blocks are allocated once
(`allocate_shared` with a pool allocator, or a fixed set of snapshots and handles).

### 4. Draw lists without per-frame allocation (~26)

Two parts:

- **The translated, clipped copy per widget** (`flatten_node_draws`). Snapshots share these
  copies (`cached_snapshot_draws`), so a widget cannot clear one a snapshot still holds — the
  clip-sharing bug fixed in November. Each widget keeps a small ring of copies and re-records
  into one no snapshot holds any more (`use_count() == 1`).
- **Command payloads.** `PolylineCmd::points`, `FilledPolygonCmd::points` and `TextCmd::text` own
  their storage, so re-recording a widget allocates per command even when the list is reused.
  The list owns one point buffer and one text buffer instead, and commands refer to ranges in
  them; `clear()` keeps both capacities. This changes the DrawList format
  ([draw-list.md](draw-list.md)): every backend (SDL, prism-stm32's rasterizer, the web build)
  and the serialisation follow.

### 5. Text set at runtime (open)

An app that changes a label's text builds a new string (the scope's timing line, once a
second), and `Field::set` moves it in. Options:

- **(a) Overwrite in place** — the label's string keeps its capacity and the app writes into it
  (e.g. `label.assign(text)` on the field); no allocation once the capacity fits. A small API
  addition. **Recommended.**
- **(b) A fixed-capacity string type for labels** — never allocates, but changes the label type
  for every app.

## Order and measures

Each phase is measured with `alloc_sites` and the board's "allocs/frame" before and after. Done
when the scope reads 0 on both, with the guard on in the embedded build. Phases 2–4 change PRISM
and land as separate commits. Fixed-capacity containers: none needed through phase 4 (a fixed
array and a count); `std::inplace_vector` is C++26 but missing from GCC 15, the board's
compiler; ETL comes back up if more appear.

## Not in scope

A static, compile-time-sized widget tree (TouchGFX's model), or replacing the allocator with a
pool (LVGL's model): the heap already sits in a fixed SRAM region on the board, and a pool
allocator does not make per-frame allocation deterministic.
