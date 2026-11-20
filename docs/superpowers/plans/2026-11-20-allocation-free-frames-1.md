# Allocation-free frames, part 1 (guard, plot, snapshot reuse, text) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phases 1, 2, 3 and 5 of the design: an embedded allocation guard, an allocation-free
empty plot redraw, snapshots rebuilt into reused storage, and in-place text changes — measured
on the prism-stm32 audio scope.

**Architecture:** PRISM gains `WidgetTree::build_snapshot(SceneSnapshot&, …)` (rebuild into an
existing snapshot, capacity kept), value parameters in the plot renderer, a fixed-capacity tick
list, and `Field::mutate`. Pooling snapshots is the runtime's job: prism-stm32's runtime keeps 4
snapshot slots. Phase 4 (draw-list payloads, per-widget copy ring) is part 2, planned after this
part is measured.

**Tech Stack:** C++26, GCC 16 host / GCC 15 board, doctest, meson; two repos: PRISM
(`~/Documents/prog/PRSIM`) and prism-stm32 (`~/Documents/prog/prism-stm32`).

**Spec:** `doc/design/allocation-free-frames.md` (PRISM).

## Global Constraints

- One code path for every build; no container-swapping flag. Startup allocation stays allowed.
- `SceneSnapshot` contract unchanged: nobody writes a snapshot while anyone holds it.
- PRISM API stays source-compatible: `build_snapshot(w, h, version)` returning
  `std::unique_ptr<SceneSnapshot>` keeps working (desktop, Python, tests).
- Text option (a): in-place change, no fixed-capacity string type.
- PRISM builds/tests on Coder only: `/tmp/claude-6516/prism_remote.sh '<cmd>'` (PRISM),
  `scripts/remote.sh '<cmd>'` (prism-stm32, which compiles the workspace's `~/PRSIM`, so run the
  PRISM sync first). One build at a time, foreground.
- PRISM style: AGENTS.md (headers keep templates, `src/` the rest; `simplify:` on cuts).
- Commits with explicit pathspecs, one per task per repo; no push.

## Review Focus

- A plot asked for many ticks (`nice_ticks(0, 1, 1000)`): returns at most the list's capacity,
  no overflow. → Task 2 test.
- Rebuilding into a snapshot that held a bigger scene (popup closed, fewer widgets): no stale
  entries survive. → Task 3 test.
- `mutate` on a field inside a transaction / from the wrong thread: same rules as `set`
  (deferred notification; wrong-thread abort). → Task 4 test.
- `mutate` that changes nothing still notifies (documented cost, not an error). → Task 4 test.
- Snapshot slots on the board: the logic thread never builds into a slot the render thread is
  drawing or that `shown` points at. → Task 5 (by construction; checked in Renode with touch).

---

### Task 1: Allocation guard (prism-stm32)

**Files:**
- Modify: `posix/alloc_count.c`, `app/alloc_count.hpp`, `tests/posix_qemu/main.cpp`,
  `meson.options`, `meson.build`, `boards/stm32h7b3i_dk/main.cpp`, `README.md`

**Interfaces:**
- Produces (C, in `alloc_count.c`, declared in `app/alloc_count.hpp`):
  `void prism_allocation_guard_arm(void)`, `void prism_allocation_guard_disarm(void)`,
  `uint32_t prism_allocation_violations(void)`, `void *prism_allocation_first_violation(void)`
  (the caller's return address of the first allocation while armed).
- When armed and built with `-DPRISM_ALLOC_GUARD_TRAP`, the wrapper also executes
  `__builtin_trap()` after recording (a debugger stops in the HardFault handler with the
  address in `prism_allocation_first_violation`).

- [ ] **Step 1: QEMU test 15** — arm, `escape(new int)`, disarm; check violations == 1 and the
  first violation address is non-null; a `new` after disarm does not count.
- [ ] **Step 2: Run; fails to link** (functions missing).
- [ ] **Step 3: Implement** in `alloc_count.c`. `posix_tests.elf` is built without the trap
  define.
- [ ] **Step 4: Run QEMU tests: 15/15.**
- [ ] **Step 5:** meson option `alloc_guard` (boolean, default false) adds
  `-DPRISM_ALLOC_GUARD_TRAP` to `posix_board`'s c_args and `-DPRISM_ALLOC_GUARD` to the board
  targets; `main.cpp` arms the guard in the logic thread's first loop iteration after the
  first snapshot — via a hook: `app::run` gains nothing; instead the scope example arms it in its
  `tick()` on the 2nd call when `PRISM_ALLOC_GUARD` is defined (`simplify:` a proper
  "startup done" hook in the runtime when a second example needs it). README: the option and
  how to read the address.
- [ ] **Step 6: Commit** (prism-stm32).

### Task 2: Allocation-free plot redraw (PRISM)

**Files:**
- Modify: `include/prism/widgets/plot_render.hpp` (`nice_ticks`, `TickArrays`),
  `include/prism/widgets/plot.hpp` (`compute_mapping`, `render_plot_panel`, `PlotModel::canvas`,
  `PlotPanel::canvas`), `tests/test_plot.cpp`
- Create: `tests/alloc_counter.hpp` (counting `operator new` for test executables), and register
  `tests/test_alloc_free.cpp` in `tests/meson.build`'s `headless_tests`
- Test: `tests/test_alloc_free.cpp`

**Interfaces:**
- Produces: `struct prism::plot::TickList { static constexpr std::size_t capacity = 32; … }` with
  `size()`, `empty()`, `operator[]`, `begin()/end()`, `front()/back()`, `push_back` (drops past
  capacity). `nice_ticks(...)` returns `TickList`. `TickArrays` holds two `TickList`s.
- `compute_mapping(Rect, AxisRange x, AxisRange y, ViewTransform view, …)` and
  `render_plot_panel(…, AxisRange x_range, AxisRange y_range, ViewTransform view,
  const Field<C>& cursor, …)` take values. Callers pass `.get()` or the resolved values.
- `tests/alloc_counter.hpp`: `std::size_t prism::test::allocations()`; the header defines the
  replaceable `operator new`/`delete` (include it in exactly one TU per test executable).

- [ ] **Step 1: Tests** in `test_alloc_free.cpp`:
  - "an empty plot re-records without allocating": a `PlotModel` with no series, labels "x"/"y",
    fixed ranges; `canvas()` into a `DrawList` once, `clear()`, count around a second `canvas()`
    into the same list → 0. (Needs a `WidgetNode` with a theme: copy the setup `test_plot.cpp`
    uses.)
  - "nice_ticks never exceeds its capacity": `nice_ticks(0, 1, 1000).size() <= 32`.
- [ ] **Step 2: Run; first test fails** (temporary `Field`s and tick vectors allocate).
- [ ] **Step 3: Implement.** Existing `test_plot.cpp` nice_ticks tests keep passing (adapt only
  if they compare against `std::vector`).
- [ ] **Step 4: Full PRISM suite green** (`meson test -C builddir`), and a no-reflection syntax
  check of the touched headers (memory: check-no-reflection-build).
- [ ] **Step 5: Commit** (PRISM).

### Task 3: Rebuild into an existing snapshot (PRISM)

**Files:**
- Modify: `include/prism/render/scene_snapshot.hpp` (`SceneSnapshot::clear()`),
  `include/prism/app/widget_tree.hpp`, `src/app/widget_tree.cpp`
- Test: `tests/test_alloc_free.cpp`, `tests/test_scene_snapshot*.cpp` or `test_widget_tree*.cpp`
  (wherever snapshot structure is tested)

**Interfaces:**
- Produces: `void SceneSnapshot::clear()` — empties every vector and the overlay, resets
  counters, keeps capacities. `void WidgetTree::build_snapshot(SceneSnapshot& out, float w,
  float h, uint64_t version)` — clears `out` and builds into it; the existing
  `std::unique_ptr<SceneSnapshot> build_snapshot(float, float, uint64_t)` becomes
  `make_unique` + this.

- [ ] **Step 1: Tests:**
  - "a snapshot rebuilt into the same storage allocates nothing when nothing changed": a tree
    with a label, a checkbox and a slider; build into `snap` twice (warm-up), count around a
    third build → 0.
  - "a rebuilt snapshot keeps nothing from the previous scene": build a tree with an open popup
    (or more widgets via a `List`), then rebuild into the same object after closing it /
    removing items; geometry, draw_lists, z_order and overlay match a fresh `build_snapshot`.
- [ ] **Step 2: Run; fail** (no such overload).
- [ ] **Step 3: Implement.** If the third build still allocates (layout, flatten helpers),
  `alloc_sites`-style attribution tells where; fix what is in the snapshot path, ledger what
  belongs to part 2.
- [ ] **Step 4: Full suite green; commit** (PRISM).

### Task 4: `Field::mutate` — change a value in place (PRISM)

**Files:**
- Modify: `include/prism/core/field.hpp`, the field reference page in `doc/concepts/reference/`
- Test: `tests/test_field.cpp`, `tests/test_alloc_free.cpp`

**Interfaces:**
- Produces: `template <typename F> requires std::invocable<F&, T&> void Field::mutate(F&&)` —
  applies the change to the stored value in place and always notifies (no equality check: no
  copy is made to compare against). Same thread check and transaction deferral as `set`.

- [ ] **Step 1: Tests:** notifies once; inside a transaction the notification is deferred like
  `set`; from another thread it aborts like `set` (reuse test_field's death/claim pattern);
  a no-op mutate still notifies; `label.mutate([](auto& l) { l.value.assign("12 fps"); })` on a
  label whose string already holds a longer text allocates 0.
- [ ] **Step 2: Run; fail. Step 3: Implement. Step 4: Suite green; docs: when to use `mutate`
  over `update` (no copy, always notifies). Commit** (PRISM).

### Task 5: prism-stm32 adopts it; measure

**Files:**
- Modify: `app/runtime.cpp` (4 snapshot slots), `app/frame_stats.hpp/.cpp` (format into a fixed
  `std::array<char, 128>`, `tick` returns `std::optional<std::string_view>`),
  `app/example.hpp` (`show_stats(std::string_view)`), `app/audio_scope.cpp` (stats via
  `mutate`), `app/widgets_example.hpp`, `host/scope_bench.cpp`, `host/alloc_sites.cpp`,
  `host/tests/test_frame_stats.cpp`, `host/tests/test_raster_allocations.cpp`

**Interfaces:**
- Consumes: Task 3 `build_snapshot(SceneSnapshot&, …)`, Task 4 `mutate`.
- Runtime slots: `std::array<SceneSnapshot, 4>`; `Handoff` holds slot indices
  (`pending`, `shown`, `drawing`, each `-1` when none) instead of `shared_ptr`s; the logic
  thread builds into the slot that is none of the three. Input hit-testing reads `shown`'s
  geometry under the mutex (copy the small geometry lookup, or hit-test while holding it).

- [ ] **Step 1:** FrameStats test: the line from `tick` is a `string_view` into its own buffer
  and two consecutive lines with the same numbers allocate 0 (host counter).
- [ ] **Step 2: Run; fail. Step 3: Implement FrameStats; scope/widgets examples; runtime slots.**
- [ ] **Step 4:** Host suite green; `alloc_sites` and `scope_bench` with the reused snapshot
  (they build into one `SceneSnapshot`); record the new per-frame count by function.
- [ ] **Step 5:** Renode: scope screenshot shows the new allocs/frame; `tap.resc` with
  `-Dexample=widgets` still counts to 2 (input still hit-tests against `shown`).
- [ ] **Step 6: Commit** (prism-stm32); write the measured numbers into the design note's
  "Where we are" (PRISM commit).
