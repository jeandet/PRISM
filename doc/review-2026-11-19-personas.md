# Persona review — 2026-11-19

Four subagent personas reviewed the repo in parallel and answered three questions each:
first impression, "can you build something easily?", "what is missing?".

- **persona_cpp_expert** — principal-level C++ (20+ years: templates, concepts, atomics, P2996, ABI)
- **persona_qt_dev** — experienced Qt dev (Widgets/QML, signals/slots, model/view, porting apps)
- **persona_wx_dev** — experienced wxWidgets dev (event tables, sizers, widget set, native L&F)
- **persona_junior_dev** — junior C++ dev; the only one that ran builds (see method)

## Method

- Static review for the three senior personas: code, docs, tests, CI, meson files only — no meson/ninja,
  per the AGENTS.md rule of never running concurrent builds against one build dir.
- The junior persona did run the documented path once, foreground, capped at `ninja -j2` because the
  host is shared and each C++26/reflection-heavy TU costs ~1 GB RAM: built
  `examples/tour/01_struct_to_ui` (~153 s cold-ish, zero warnings under `-Werror`), ran it headless
  (`SDL_VIDEODRIVER=offscreen`, runs its event loop until killed), then compiled the README Counter
  from `/tmp` in ~11.5 s by adapting the flags from `ninja -C builddir -t commands` and ran that too.
- All file:line references below are as reported by the reviewers.

## Verdict at a glance

| Question | Consensus |
|---|---|
| First impression | Serious, unusually self-critical project; the README's 10-line demo is literally true; architecture claims survive contact with the code |
| Can you build something easily? | **Yes, with friction — environmental, not API** (junior proved it end-to-end). Friction: GCC 16 required for reflection auto-UI, meson-only integration, wrap downloads, ~1 GB RAM/TU so `-j` must be capped, `-Werror` + Meson ≥1.8 `werror` quirk |
| What is missing? | Toolchain gate (GCC 16), no CMake/pkg-config path, no native chrome/a11y/i18n/printing, data-widget depth below Qt model/view, whole-tree publish + whole-window repaint perf debt, and a few correctness sharp edges (debug-only thread assert, `const_pointer_cast` in snapshot clipping) |

---

## persona_cpp_expert

### First impression

The repo reviews itself in public and mostly acts on it: `AGENTS.md` is a real engineering document
(the `detail`-namespace collision trap, the no-inline-variable rule with the Python-extension deadlock
as the reason, implemented in `src/core/shared_state.cpp`), and `doc/review-2026-11-13.md` is a candid
self-review whose "Done" items the expert spot-checked against the code (publish batching
`src/app/model_app.cpp:310`, logic-thread assert `include/prism/core/field.hpp:33`, subtree dirty
marks `include/prism/ui/widget_node.hpp:34-70`, `adopt()` assert `src/app/widget_tree.cpp:448`).

Impressive: the tour is real (`examples/tour/01_struct_to_ui.cpp` is ~15 lines of actual code);
the render/logic thread split is implemented as documented (immutable `SceneSnapshot` via
`atomic_cell<shared_ptr>`, input forwarded onto a `stdexec::run_loop`, `src/app/model_app.cpp:240-246,378`);
the reactive core is small enough to audit — 27 headers / 2,244 lines for `Field`/`State`/`Derived`/
`Shared`/`Channel`/`List`/`Connection`/`Latest` — and the tricky parts check out: correct
Michael-Scott MPSC (`core/mpsc_queue.hpp`), correct 3-slot SPSC ring (`core/latest.hpp:33-52`),
correct acq_rel pending-flag protocol in `Shared` (`core/shared.hpp:30-47`); `Scalar<Tag>` geometry
(`core/types.hpp:8-101`) turns `X + Y` into a compile error; widget dispatch as `Widget<T>`
specializations (`ui/delegate.hpp:506-525`) is a defensible no-vtable design; P2996 is quarantined
behind `__cpp_impl_reflection` (`meson.build:52-60`) with a first-class `view()` fallback; 186 test
files / 2,181 `TEST_CASE`s plus property-based pixel tests (`tests/test_pixels.cpp`) and a TSAN lane.

Odd/worrying: `render/scene_snapshot.hpp:78` does `std::const_pointer_cast<DrawList>` on draw lists
shared with the previous snapshot — safe only via the `!clipped_by_ancestor` invariant
(`ui/layout.hpp:654-669`); one broken invariant is a cross-thread data race, not a logic error.
"Lock-free" is overstated (`core/atomic_cell.hpp:14-33` — mutex fallback on mainstream stdlibs);
`SoftwareBackend::request_window` blocks the logic thread up to 2 s (`src/backends/software_backend.cpp:113-121`);
`SenderHub::emit` copies the whole callback vector under a mutex per emit (`core/connection.hpp:92-104`);
`Field<T>::set` silently requires `operator==` (`field.hpp:35`); off-logic-thread `set` is
assert-only — a silent data race in release; no cycle guard outside `transaction()` and the release-mode
cap silently drops notifications (`core/transaction.hpp:58-80`); nine `builddir*` dirs in the tree,
only some gitignored.

### Can you build something easily?

Yes, with friction — toolchain/first-build, not API. Commands as advertised and consistent across
`README.md`, `AGENTS.md`, `doc/start/install.md`; reflection self-probes via `__cpp_impl_reflection`
(nice catch: Emscripten accepts `-freflection` without implementing it); consuming as a subproject is
three lines of meson. Friction: GCC 16 for the headline feature (main CI lane runs g++-14 without
reflection); Meson ≥1.8 needs `-Dsdl3:werror=false` on the command line (`meson.build:1-7`); deps are
network-fetched source wraps (SDL3, freetype, harfbuzz) and the SDL3 headless-driver patch can be lost
by `meson wrap update`; ~1 GB+ RAM per TU; `werror=true` at `warning_level=3`. Writing the app itself
is genuinely easy — the five tour programs are 24–40 lines and check out against the headers.
`examples/model_system_monitor` (229 lines) is a believable real app, with one honest wart: a dummy
`Field<uint64_t> process_revision` bumped by hand just to redraw the table.

### What is missing? (ranked)

1. Publish pipeline still does whole-tree work per publish; draw-list cache disabled under any clip;
   no damage rects, no glyph atlas, per-string text textures (authors' numbers: 1.8 ms unchanged /
   7 ms scroll at 50k widgets; `src/backends/sdl_window.cpp:300`).
2. Input dispatch heap-allocates per event (`exec::start_detached` per SDL event, `model_app.cpp:378-390`).
3. Core reactivity sharp edges: per-emit allocation (`connection.hpp:92-104`), no non-transaction cycle
   guard, `==`-requirement, debug-only thread assert, unbounded `Channel`, `Derived` re-entrancy unguarded.
4. Threading invariants softer than the docs claim: mutex-based `atomic<shared_ptr>` fallback,
   2 s `request_window` block, `const_pointer_cast` in `resolve_clips`.
5. Product gaps (mostly the authors' own list): no i18n, no printing, no OS-initiated drags,
   accessibility tree without bounds (`include/prism/app/access_tree.hpp:23`).
6. Test/infra: pixel tests depend on the hand-patched SDL3 wrap surviving `meson wrap update`;
   race coverage relies on TSAN + the multi-threaded example apps.

### Overall

Architecture claims survive contact with the code; the reactive core is auditable and its real edges
are either documented or genuine gaps; C++26 usage is idiomatic, not ornamental. If the
review-2026-11-13 discipline continues, the gaps are tractable. If it ever ships without that culture,
the two places to put permanent tripwires on first are the `const_pointer_cast` in `resolve_clips`
and the debug-only logic-thread assert.

---

## persona_qt_dev

### First impression

Not a toy: one-author, agent-assisted, with real discipline — HEAD `3a6c1e2`, ~140 doctest files
(2,181 `TEST_CASE`s), 11 CI jobs (gcc16-reflection, clang-libc++, wasm-node, tsan, asan,
free-threaded, wheels, sdist, macos, windows), vendored wraps, nanobind Python bindings,
benchmarks, pixel tests, and a self-review that measures its own pipeline (50k-widget publish
46 ms → 1.8 ms after dirty-mark skipping). The README's "R&D project, nothing production-ready"
warning is the right framing.

Impressive: the wedge is real (24-line tour program becomes a window via `app::run(settings, {.title=…})`,
`include/prism/app/run.hpp:88-99`); coherent MVB architecture — persistent dirty-marked widget tree
(`ui/widget_node.hpp:35-70`), immutable `SceneSnapshot` handed to the render thread through one
`atomic_cell` swap with structural sharing; typed no-moc reactivity — `core::Wiring` is documented as
"Qt's context object, as a member" (`core/wiring.hpp:11-13`), `Connection` is RAII and may outlive its
hub; `Derived<T>` auto-tracks what its lambda reads; docs are a genuine manual with live WASM demos.

Odd/worrying: headline feature needs GCC 16 only (`meson.build:44-53`; clang/emscripten accept the flag
without implementing it); ~1.5 GB RAM/TU under sanitizers; `SenderHub::emit` per-emit mutex+vector copy
(`connection.hpp:100-110`); unbounded `Channel`; debug-only thread assert; whole-window repaint and
per-string text cache (`src/backends/sdl_window.cpp:319-330,427-446`); some design docs marked
"Superseded", review line numbers drifted after its own refactors.

### Can you build something easily?

Yes, with friction — for a form/panel/dashboard app on GCC 16; otherwise "with friction". Documented
path is exactly `meson setup builddir && ninja -C builddir && meson test -C builddir`; subproject
consumption is 3 lines + `#include <prism/prism.hpp>`; feature libs are clean meson options. Friction
found statically: compiler gate, `-Werror` + the Meson 1.8 `werror` bug, compile RAM, patched vendored
SDL for headless tests, wrap-update hazards. Docs/learning curve strong — a settings-style app in a day
*if* GCC 16 is available; without reflection, one `view()` per struct (shown in tour 01 itself).

Qt-porting notes: slots → `wire.on(field, fn)` / `wire.link(a.level, b.gain)`; gain no moc, type-safe
lambdas, RAII connections; lose `AutoConnection`/queued semantics (cross-thread you pick explicitly
among `Shared<T>`/`Channel<T>`/`ctx.post`), synchronous emit has no queued breaker, and `Derived` has an
acknowledged diamond glitch. Model/view: adapter concepts (`TableSource`/`TreeSource`/`PlotSource` +
reflection tier over `List<Struct>`) instead of `QAbstractItemModel`; lose proxy models, persistent
indexes, per-cell delegate widgets, MIME drag. Layout: `vb.vstack/hstack/spacer/gap/handle/tabs/toolbar/
status_bar/group/section/dock` with real size policies; lose grid/form layouts and `QSplitter`-style
collapse. Threading: PRISM keeps the *window* alive when logic is stuck but freezes *content*;
macOS swaps the thread roles because Cocoa requires SDL on the main thread.

### What is missing? (ranked, from the port-a-Qt-app view)

1. Toolchain gate for reflection auto-UI (biggest adoption wall).
2. Accessibility bridge — metadata exists (`ui/access.hpp`, `app/access_tree.hpp:23` "no bounds yet"),
   screen-reader bridge deferred. Hard blocker for enterprise/public-sector ports.
3. i18n — no `tr()` equivalent; gettext-vs-Fluent undecided; "Ctrl" hardcoded on macOS (`ui/shortcut.hpp:113`).
4. Printing — none.
5. Model/view depth — no proxy models / persistent indexes; cells limited to text/checkbox/image.
6. Reactivity runtime costs/semantics — per-emit allocation, unbounded `Channel`, debug-only assert,
   `Derived` diamond glitch.
7. Render efficiency — whole-window repaint, per-string textures, no damage rects/glyph atlas.
8. Widget-catalog gaps — toolbar/status bar/pickers/etc. are done per the review's Done notes; still
   absent: MDI (multi-window + docking instead), system tray, tri-state checkbox, grid layout,
   stylesheet theming, global undo framework.
9. WASM caveats — real differentiator vs Qt WASM (threads work), but needs JSPI browsers
   (Chrome 137+/Firefox 153+/Safari 27+) and COOP/COEP (hence the coi-serviceworker hack).
10. Docs drift in the design layer.

### Overall

The most serious from-scratch Qt alternative seen in this space — not because of the demo, but because
the architecture underneath is coherent and self-critical. Port decision hinges on: GCC 16 (or accept
hand-written `view()`), accessibility + i18n needs (both deferred), and rewriting model code around
`TableSource`/`TreeSource` adapters. Gain: the window stays alive when the logic thread is stuck, and
cross-thread handoff is explicit and auditable. For a new instrument/monitor/internal tool on Linux
with GCC 16: prototype in PRISM today; for a shipping product with a11y/i18n/printing requirements: wait.

---

## persona_wx_dev

### First impression

What lands first is the honesty: the README's R&D warning above the fold and `doc/review-2026-11-13.md`
with measured numbers, known races, and "waiting on a decision" items. Repo stats measured: ~34k lines
in `include/`+`src/`, 183 test files, 23 wraps.

Impressive: the tour is real (23–39 lines per program, each pointing at the at-scale app); coherent
end-to-end architecture — `Field<T>` + `SenderHub` (`core/field.hpp:100-108`), `Widget<T>`
specializations with `record(DrawList&, …)`/`handle_input(…)` (`ui/delegate.hpp`), measure/arrange/
flatten into an immutable `SceneSnapshot` published via `atomic_cell`; from-scratch software
rasterizer, real text stack (SumTree + HarfBuzz + FreeType), SDL3 backend, optional WebGPU, WASM
build, Python bindings. Docs are a manual, not a blog; tests mirror headers ~1:1 including pixel tests.

Odd/worrying: flagship feature needs GCC 16 `-freflection` (else manual `view()`,
`examples/tour/01_struct_to_ui.cpp:12-15`); `werror=true` + `warning_level=3` plus the Meson 1.8+
`werror` bug meaning `-Dsdl3:werror=false` must be passed explicitly; ~1.0–1.5 GB RAM/TU (3.5 GB gallery
main under sanitizers); thread roles inverted vs wx — the thread calling `app::run` becomes the
SDL/render thread (`app/app.hpp:104-112`, `app/model_app.hpp:391-396`), mandatory on macOS; cross-thread
`Field::set` is assert-only (`core/field.hpp:33`); custom-drawn window chrome by default
(`DecorationMode::Custom`, `doc/app/windows.md`), menu bar is an `hstack` of `ui::Menu` widgets
(`doc/widgets/popups/menu.md:1-3`) — no OS menubar, no native look.

### Can you build something easily?

Yes, with friction — toolchain and build hygiene, not API. Documented commands cross-checked against
build files; consumption is three lines (drop into `subprojects/`, `prism_dep = subproject('prism')…`,
`#include <prism/prism.hpp>`, `doc/start/install.md:60-70`); feature libs are opt-in options. Friction:
cold build compiles SDL3+freetype+harfbuzz from wraps; toolchain gate; `wrap update` can silently drop
the pinned SDL3 headless patch; `werror` makes your own TUs warning-clean-or-fail. Extras that smooth it:
`app::run(model, {.title, .width, .height, .settings=&s, …})` gives window + TOML settings + remembered
geometry in one call (`include/prism/app/run.hpp:86-99`); debug inspector on Ctrl+Shift+I.

### What is missing? (ranked)

1. **Native platform integration** — the biggest wx-parity gap: custom title bar (with a Wayland
   warning), own menu bar, no macOS global menu, no native look & feel; accessibility tree has no
   bounds.
2. **Toolchain gate** on "struct = UI" + ~1 GB RAM/TU compile cost.
3. **Widget catalog gaps** vs a typical wx main window — present: frame with custom chrome, toolbar,
   status bar, tabs, tree (checkboxes/rename/multi-select), table (sortable, editable, checkbox/icon
   cells, column show/reorder), multiline text + full editor, plot, node editor, docking. Missing:
   ribbon, property grid as a user-facing widget, HTML/webview, printing, translations.
4. **Performance honesty at scale** — one publish per input event until recently; whole-tree walks
   (46 ms → 1.8 ms at 50k widgets); full-window repaint; per-`set` allocation in `SenderHub::emit`
   (`connection.hpp:92-108`); unbounded `Channel` (`core/channel.hpp:53-56`).
5. **Threading caveats for a wx dev** — a slow logic callback freezes the *content*, not the window
   (`doc/concepts/threads.md:25-27` says so plainly); safe handoffs are `Shared<T>`/`Channel<T>`/
   `ctx.post`/`Latest<T>`; thread roles inverted vs wx.
6. Smaller gaps the authors list themselves: submenus open right only, "Ctrl" on macOS, combo
   suggestions cap at 8, shortcuts on hidden tabs.

### Overall

The most credible "replace wx from scratch" project reviewed here, mainly because it doesn't pretend to
be finished. Day-one experience is excellent; the wx translations are clean — "button updates label"
becomes `button.observe([this]{ ++label; })`, no id/`Skip()` dance, and layout via
`vb.vstack/hstack/spacer/gap/handle/toolbar/status_bar/group/section/dock` covers a real dialog more
declaratively than sizers; custom drawing via retained, dirty-tracked, strong-typed `DrawList`s
(`vb.canvas`) is a genuine upgrade over paint-event + wxDC for plots. The two things that would stop a
wx replacement today: the GCC-16-or-manual-`view()` gate, and the lack of native chrome/menubar/look-and-
feel plus the still-young perf story.

---

## persona_junior_dev

### First impression

The README's 10-line Counter is the whole pitch — and it's true (they built it). The bold "R&D project,
not production-ready" banner is the first thing seen. Scale is impressive: ~104k lines across
`include/ src/ tests/ examples/ doc/`, 117 doctest files mirroring headers ~1:1, and a 388-line candid
self-review with measured perf bugs. Odd: eight `builddir-*` dirs at the root plus the "never build
concurrently" rule is an operational footgun on a shared machine; the headline feature is compiler-
locked to GCC 16 (`meson.build:49-59`), and even `[[=core::range<…>]]` doesn't parse on clang 22
(review item 18); `Field::set` off the logic thread is debug-assert-only (`core/field.hpp:21-25`,
admitted in `doc/concepts/threads.md:14-18`).

### Can you build something easily? — YES, with minor friction (all commands really run, `-j2`)

1. Staleness check: `ninja -C builddir -n 01_struct_to_ui | wc -l` → 2 (regen); friction #1 — the target
   is `examples/tour/01_struct_to_ui`, the bare name fails with `ninja: error: unknown target`.
   After regen the dry run showed 310 steps (297 compiles).
2. `ninja -C builddir -j2 examples/tour/01_struct_to_ui` → success, zero warnings, **~153 s** wall
   on a 16-core box shared with a heavy unrelated Qt build (mostly SDL3 + libprism; the tour TU is seconds).
3. Headless: `SDL_VIDEODRIVER=offscreen timeout 5 …` → `exit=124`, i.e. the app ran its event loop
   until killed, no crash (offscreen driver from the `sdl3-headless-drivers.patch`). No `--headless`/
   auto-quit flag exists in `AppConfig` (`include/prism/app/run.hpp:51-60`).
4. Own first app: README Counter copied to `/tmp/my_counter.cpp`, compile command adapted from
   `ninja -C builddir -t commands examples/tour/01_struct_to_ui` → **compiled clean in 11.5 s** under
   `-Werror`, no missing flags; link needed rpaths rewritten because the binary lives in `/tmp`;
   ran headless fine. A deliberate type error produced a clean, helpful GCC diagnostic pointing into
   `core/field.hpp:53`.
5. Tests: adding one = one line in the `headless_tests` map (`tests/meson.build:25`) + a `test_*.cpp`
   following `tests/test_field.cpp:1-16` (whose `namespace prism::core {}` preamble must be copied
   blindly — exists because of the no-bare-`detail` rule in AGENTS.md).

### What is missing? (ranked from the junior seat)

1. **No CMake / pkg-config / conan consumption path** — meson subproject only (`doc/start/install.md:55-59`);
   biggest adoption gap for CMake-land juniors; first `meson setup` also downloads all wraps (network + time).
2. **Headline feature needs GCC 16**; elsewhere `view()` by hand; attribute syntax not portable to clang 22.
3. **Model thread-safety is debug-assert-only** — silent data race in release (`field.hpp:21-25`);
   the one rule every newcomer will break.
4. **No headless/auto-quit mode in the public API** — binaries run forever (`exit=124`); headless capture
   exists only in `examples/showcase/showcase_common.hpp:12`. Gap for CI / headless boxes.
5. **Build ergonomics** — full-path target names, silent regen on stale builddir, eight build dirs +
   the no-concurrent-builds rule, ~1 GB/TU so `-j` must be capped (the operator had to say so explicitly).
6. **Product gaps the author lists as undecided** (review §5): no OS-level cross-window drag, no i18n,
   no printing, no accessibility bridge (`app/access_tree.hpp:23`).
7. Docs assume meson fluency and C++26 (designated initializers, reflection, `std::expected`, stdexec);
   the install page says so honestly, but there is no "I just have CMake and GCC 14" path beyond the
   one-paragraph `view()` fallback.

### Overall

The rare project whose README demo is literally true: tour built in ~2.5 min at `-j2` on a busy shared
machine, run headless without errors, own Counter compiled clean in ~12 s and run. What keeps it from
"easy" for a typical junior is environmental, not conceptual: GCC 16 for the flagship reflection feature,
meson-only integration with wrap downloads, capped parallelism from ~1 GB/TU, and the debug-only guard
on the model-thread rule. As an R&D project it's impressive and usable today; as a team daily-driver on
mixed toolchains, those five gaps are the work.

---

## Cross-persona synthesis

### Build verdict

All four: **yes, easily in spirit, with environmental friction in practice.** Junior's real evidence:
~153 s build at `-j2`, clean `-Werror` compile of a from-scratch Counter in ~12 s, headless run OK.
Shared friction list: GCC 16 gate for reflection; meson-only + wrap downloads; ~1 GB RAM/TU (cap `-j`);
`-Werror` + Meson ≥1.8 `sdl3:werror` command-line workaround (`meson.build:1-7`); full-path ninja
target names; no headless/auto-quit flag; no CMake/pkg-config.

### Ranked gap list (union of the four, deduplicated)

1. Toolchain gate: reflection auto-UI requires GCC 16+ (`meson.build:49-59`); everywhere else a manual
   `view()` per struct; `[[=core::*]]` attributes not portable to clang 22.
2. Consumption: meson subproject only — no CMakeLists.txt, no `.pc` file.
3. Native platform integration: custom-drawn chrome/menubar by default, no OS menubar (macOS global
   menu), no native L&F; accessibility bridge deferred, tree without bounds (`app/access_tree.hpp:23`);
   no i18n, no printing, no OS-initiated drags (authors' own §5 list).
4. Data-widget depth: no proxy models/persistent indexes/per-cell delegates (Qt view); cells limited to
   text/checkbox/image (`ui/table.hpp:30-46`); no ribbon/property grid/webview (wx view); no grid/form
   layout, no tri-state checkbox, no MDI/system tray.
5. Performance: whole-tree publish per publish + whole-window repaint per frame, no damage rects or
   glyph atlas; per-emit callback copy in `SenderHub::emit` (`core/connection.hpp:92-108`); per-event
   heap allocation in input dispatch (`src/app/model_app.cpp:378-390`); unbounded `Channel`
   (`core/channel.hpp:49`).
6. Correctness sharp edges: cross-thread `Field::set` is assert-only → silent race in release
   (`core/field.hpp:33`); `Field<T>` silently requires `operator==` (`field.hpp:35`); release-mode
   transaction cap silently drops notifications (`core/transaction.hpp:58-80`); no cycle guard outside
   transactions; `const_pointer_cast` on cross-snapshot-shared draw lists (`render/scene_snapshot.hpp:78`)
   — one broken `clipped_by_ancestor` invariant away from a cross-thread race; "lock-free" overclaim in
   `atomic_cell.hpp:14-33`; 2 s `request_window` block (`software_backend.cpp:113-121`).
7. CI/headless ergonomics: no auto-quit in `AppConfig` (`app/run.hpp:51-60`); pixel tests depend on the
   hand-patched SDL3 wrap surviving `meson wrap update`.
8. Housekeeping: several `builddir-*` dirs not gitignored; design docs drift faster than they are
   re-synced (some marked "Superseded"); duplicated review docs at root vs `doc/`.

### What each persona would do next

- **C++ expert:** keep the review discipline; put permanent tripwires on `resolve_clips`' `const_pointer_cast`
  and the debug-only logic-thread assert; fix per-emit allocation and the whole-tree publish path.
- **Qt dev:** prototype an internal Linux/GCC-16 tool in PRISM today; wait for the accessibility bridge,
  i18n and printing before porting a shipping desktop product; rewrite data code around the
  `TableSource`/`TreeSource` adapters.
- **wx dev:** best day-one experience of the three; blocked on native chrome/menubar/look-and-feel and
  the GCC-16 gate; would appreciate docking (already ahead of wx) and the retained `DrawList` canvas.
- **Junior dev:** the README demo is real; ship a CMake/pkg-config path, a headless/auto-quit flag, and a
  release-mode guard (or a loud error) on cross-thread model mutation, and the environmental friction
  mostly disappears.
