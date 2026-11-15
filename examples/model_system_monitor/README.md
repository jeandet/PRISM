# model_system_monitor Example

A fancy-htop: two background threads poll `/proc` and publish into PRISM's cross-thread cell,
`Shared<T>`, while the UI thread drains it into live plots, a sortable table, and a process tree.
The most realistic, multi-threaded example in this directory.

Where there is no `/proc` (the web page, macOS, Windows) the same threads read a simulated
machine instead (`sim_metrics.hpp`): smooth curves, and processes that start and stop.

<p align="center"><img src="../../doc/screenshots/system_monitor.svg" alt="model_system_monitor screenshot" width="700"/></p>

## Overview

Two `std::jthread`s (`poll_system_loop`, `poll_processes_loop` in `proc_metrics.hpp`) read
`/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, and per-process `/proc/<pid>/{stat,status}`,
computing CPU/mem/net deltas and a full process list, then publish into two `Shared<T>` cells.
The loops take their reader as a parameter: `proc_system_reader()` and `read_process_list` on
Linux, `simulated_system_sample` and `simulated_processes` elsewhere (`main()` picks with
`has_proc()`).
The UI thread never touches `/proc` directly — it only ever receives what the sampler threads
set, through `ctx.receive()`.

## Walkthrough

**Cross-thread ingest.** `SystemMonitor` holds its background-fed state in `Shared<T>` and
`Channel<T>`, not `Field<T>`: the sampler threads set them, and only the logic thread touches
the model.

```cpp
struct SystemMonitor {
    core::Shared<SystemSample> sys_sample{};
    core::Shared<std::vector<ProcessInfo>> proc_list{};
    core::Channel<ProcessEvent> process_events{};
    ...
};
```

**Ingest → derived state.** One call per value in `main()`'s setup. `ctx.receive()` wakes the
logic thread when a sampler sets the value and hands it to the callback there:

```cpp
ctx.receive(app.sys_sample, [&app](const SystemSample& s) { app.ingest_system(s); });
ctx.receive(app.proc_list, [&app](const std::vector<ProcessInfo>& p) { app.ingest_processes(p); });
```

`ingest_processes()` feeds one process list to two views, a table and a tree:

```cpp
void ingest_processes(const std::vector<ProcessInfo>& processes) {
    process_table.update(processes);
    process_revision.set(process_revision.get() + 1);   // the table depends_on() it: it redraws
    tree_source.update(processes);
    tree_ctrl.refresh();
}
```

`process_table` (`ProcessTable`, in `process_table.hpp`) is a hand-written `ColumnStorage`: four
columns, formatted cells. It needs no reflection, so the monitor builds with any C++23 compiler
and in the browser. `tree_source` (`FlatProcessTreeSource`, in `process_tree_source.hpp`) builds
a parent→children index from the same flat `ProcessInfo` list via `build_process_tree_index()`,
giving the "Tree" tab a hierarchical view of the exact same data the "Table" tab shows flat.

**Multiple plots, one dependency pattern**, using the variadic form of `.depends_on(...)`:

```cpp
vb.canvas(cpu_plot)
    .depends_on(cpu_plot.x_range, cpu_plot.y_range, cpu_plot.view,
                cpu_plot.cursor, cpu_plot.revision)
    .min_size(core::Height{120});
```

**The heartbeat** — a third `canvas()`, this one driven purely by the animation clock rather
than any data at all, proving the same `canvas()` + `.depends_on()` pattern covers "redraw
because a clock ticked" just as well as "redraw because a field changed":

```cpp
ctx.clock().add([&app](ui::AnimationClock::time_point now) {
    double phase = std::fmod(t * 4.0, 2.0 * std::numbers::pi); // ~4 rad/s
    app.heartbeat_phase.set(static_cast<float>(phase));
    return true; // the heartbeat runs forever by design
});
```

The `std::fmod` before the cast to `float` matters: `now.time_since_epoch()` is `steady_clock`
time since boot, which on a long-uptime machine is large enough that casting straight to `float`
loses precision below one frame's worth of phase advance — silently freezing the heartbeat
(`Field<T>::set()` no-ops on an unchanged value) instead of animating it smoothly.

**Headless capture.** `main()` seeds a small hard-coded process list (`seed_demo_data()`) before
capturing, rather than relying on the two background pollers — a static single-frame capture
can't wait for `/proc` polling to land real data:

```cpp
if (argc >= 2) {
    app.seed_demo_data();
    return showcase(argc, argv, app, 900, 700);
}
```

## Key concepts

- `Shared<T>` — the lock-free cross-thread cell; `.get()`/`.set()` from any thread, `.observe()`/`drain_notifications()` on the owning thread. See the root README's [Reactive core](../../README.md#reactive-core).
- `ctx.receive(shared_or_channel, callback)` — what a worker thread sets reaches the logic thread, with no wiring to forget.
- A hand-written `ColumnStorage` (tier 1) and `.depends_on()` — the table redraws when the list is replaced.
- Readers passed to the sampler loops — the real `/proc`, or a simulated machine, behind one loop.
- Variadic `.depends_on(a, b, c, ...)` — one call instead of chained `.depends_on()`s.
- `canvas()` driven by an `AnimationClock` tick instead of a `Field<T>` change — the heartbeat.

## Building and running

```bash
ninja -C builddir examples/model_system_monitor/model_system_monitor
./builddir/examples/model_system_monitor/model_system_monitor
```

Pass an output path as `argv[1]` (as the `svg_system_monitor` custom target does) to run
headlessly and dump a single-frame SVG snapshot instead of opening a window.

## See also

- [`model_notes`](../model_notes/) — the same `TreeController`, over the filesystem instead of an in-memory process list.
- [`widget_gallery`](../widget_gallery/) — tabs, a table and a tree again, fed by user input instead of background threads.
