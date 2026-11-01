# Wait-free snapshot handoff — design

**Status:** deferred after measurement (see below). Build it when the second backend starts.
Plan item P1 #0 in `doc/review-2026-11-01.md`.
**Origin:** drafted by opencode, challenged and amended by Claude Code (amendments marked ⟶).

## Why

The frame contract says the app never blocks the renderer. Today it can:

1. `SoftwareBackend::submit()` (app thread) takes `windows_mutex_`.
2. The per-window slot is `core::atomic_shared_ptr`, lock-based in libstdc++ (and a mutex on libc++).
3. If the app thread is preempted while holding either, the render thread waits for it —
   up to a scheduler time slice.

## Measurement (2026-11-01)

A temporary probe timed every contended wait on `windows_mutex_`. `perf_lab` ran on the real
`SoftwareBackend` with SDL's offscreen driver (system `libSDL3.so.0` via `LD_PRELOAD`, since the
vendored SDL build has no offscreen/dummy driver), 20 s per run, default perf_lab load:

| Run | Render-thread acquisitions | Contended | Max wait |
|---|---|---|---|
| Unloaded | 1083 | 1 | 17 µs |
| perf_lab + 4 busy loops pinned to the same 2 cores | 663 | 0 | 0 |

The lock costs nothing measurable. Under overload the render thread lost frames to CPU
starvation, not to the lock. Limit of the method: the bad case (app thread preempted *inside* a
~1 µs critical section) is rare, so 20 s cannot rule it out; its worst case is one scheduler
slice (a few ms) on an occasional frame — below one 16 ms frame.

## Decision

Not worth a 200-line rewrite of the SDL backend now. SDL will not stay the only backend, which
changes where this belongs, not whether: the mailbox goes into a **backend-neutral component**
(next to `SceneSnapshot`, used by every backend), built when the second backend is started and
the handoff has to be extracted anyway. Then each backend supplies its own wake: SDL keeps
`SDL_PushEvent` (lock inside SDL), a native backend can wake its render thread with a
lock-free primitive (eventfd, futex, `std::atomic::notify_one`), which makes the whole path
wait-free there.

Re-open earlier if a measurement shows a stall (rerun the probe recipe above, with longer runs).

## Key fact

The window map is already owned by the render thread. Window creation and close requests are
queued and drained there. Only two calls cross threads: `submit()` (writes) and
`present_stats()` (reads). Both touch one per-window object. Move that object out of the map and
`windows_mutex_` has no users left.

## Design (for the backend-neutral version)

Per window, a `SnapshotMailbox` with a stable address, never freed:

- `std::atomic<MailNode*> pending` — app → render, newest wins (coalescing).
- `std::atomic<MailNode*> spare` — recycles the node so steady state does not allocate.
- `std::atomic<bool> closed` — tombstone; the mailbox outlives its window.
- `mpsc_queue<shared_ptr<const SceneSnapshot>> retired` — render → app, so big snapshots are
  freed on the app thread, not inside the render thread's frame budget.
- Present stats as relaxed atomics (already atomics today).
- Render-thread-only: `current` snapshot (for redraws), `force_redraw` flag.

Operations:

| Op | Thread | Steps | Progress |
|---|---|---|---|
| `submit` | app | take spare (or `new` first time), `pending.exchange(node)`, delete displaced node, drain `retired`; re-check `closed` and retake its own node if a close raced | wait-free (bounded RMWs) |
| `take_pending` | render | `pending.exchange(nullptr)`; push old `current` to `retired`; recycle node into `spare` | wait-free |
| `present_stats` | app | relaxed loads | wait-free |
| lookup by id | any | two atomic loads (see table) | wait-free |

Ownership rule: only the thread whose `exchange` removed a node may free it.

⟶ **Lookup table: grow-only two-level table, not a fixed 32-slot array.** Window ids are never
reused, and the debug inspector creates a new window on every toggle; a fixed table would stop
accepting submits after 32 window creations. Use `std::atomic<Chunk*> chunks[1024]`, chunks of 64
mailboxes allocated on demand by the render thread and never freed (65 536 window creations per
process; beyond that, `submit` is a no-op with an assert). Each mailbox is ~100 bytes.
`simplify:` comment names the ceiling.

⟶ **Honest boundary.** `wake()` is `SDL_PushEvent`, which takes SDL's event-queue lock. PRISM's
own handoff becomes wait-free; SDL's short internal lock remains. Say this in the header.

Render-thread hot path after the change: no `windows_mutex_` at all (today it is taken ~10 times
per loop iteration, plus once per SDL event in `sdl_id_to_prism_id`, which becomes a plain
render-thread map).

`BackendBase` is unchanged, so the null/test/capturing backends and Python need no changes.

## Tests to write first

1. New `tests/test_software_backend_mailbox.cpp` (no window needed):
   submit coalesces to the newest; unknown id is a no-op; submit after close does not
   resurrect and leaks nothing (ASan); retired snapshots are destroyed on the submitting thread
   (custom deleter records the thread id); a taken snapshot outlives its node;
   ⟶ more than 64 window ids (crosses a chunk) still work.
2. Extend `tests/test_software_backend_concurrent.cpp`: 4 submitters + 1 taker, every take is a
   valid snapshot, versions monotone per writer (TSan lane).
3. New gated `tests/test_software_backend_lifecycle.cpp`: close during a submit storm, no leak/UAF.
4. Extend `tests/test_software_backend_present_stats.cpp`: `present_stats` polled during a storm.

## Measure before and after

`perf_lab --headless`: p99 of `last_present_ms`, plus a hold-time probe on `windows_mutex_`
before the change. The change is justified by the contract, but a p99 delta is what proves it.

## Not worth it if

The measurement shows no stall *and* nobody needs the guarantee. Then keep it as a documented
known limitation instead.
