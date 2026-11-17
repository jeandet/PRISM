# Examples

Eight complete apps, each with its own README. All of them also run in the browser, compiled to
WebAssembly: see the [live demos](https://jeandet.github.io/PRISM/).

New to PRISM? Read the [tour](tour/) first: five programs of 20 to 40 lines, one idea each, in
order. Each one ends by pointing at the app below that does the same thing at scale.

<table>
<tr>
<th>Example</th>
<th>Screenshot</th>
<th>Demonstrates</th>
</tr>
<tr>
<td><a href="widget_gallery/">widget_gallery</a></td>
<td><img src="../doc/screenshots/widget_gallery.svg" width="180"/></td>
<td>Every widget, in ten tabs: controls with tooltips, text in several scripts (right to left
too), a code editor in six languages with find and replace (and a 100,000-line file to type in),
Markdown edited as rich text, a table, tree and list, a plot and a canvas, menus, dialogs (one holding a form), a popover,
file dialogs, drag and drop, and a dock whose panels the user rearranges. Pick a widget here.</td>
</tr>
<tr>
<td><a href="model_notes/">model_notes</a></td>
<td><img src="../doc/screenshots/model_notes.svg" width="180"/></td>
<td>A text editor: file tree, <code>TextArea</code> with selection, clipboard, undo and input
methods, search highlights, menus with Alt shortcuts, a rename dialog, file dialogs, drop to open</td>
</tr>
<tr>
<td><a href="model_system_monitor/">model_system_monitor</a></td>
<td><img src="../doc/screenshots/system_monitor.svg" width="180"/></td>
<td>Background threads feeding the UI through <code>Shared&lt;T&gt;</code> and <code>Channel&lt;T&gt;</code>:
live plots, a sortable process table and tree, an event log. Linux <code>/proc</code>, or a simulated
machine elsewhere</td>
</tr>
<tr>
<td><a href="perf_lab/">perf_lab</a></td>
<td><img src="../doc/screenshots/perf_lab.svg" width="180"/></td>
<td>100k-row table, 1M-point plot and 1 kHz telemetry, a live FPS/build-stats bar, and a
<code>--headless N</code> report</td>
</tr>
<tr>
<td><a href="model_lab_bench/">model_lab_bench</a></td>
<td><img src="../doc/screenshots/lab_bench.svg" width="180"/></td>
<td>A bench instrument: an oscilloscope drawn on a canvas, a power supply and a function generator
driven by <code>Derived</code>, and a hardware thread feeding frames through
<code>Shared&lt;T&gt;</code>. Two buttons stall the hardware thread or the logic thread, so the
difference between the two is something you watch rather than read about</td>
</tr>
<tr>
<td><a href="model_synth_rack/">model_synth_rack</a></td>
<td><img src="../doc/screenshots/synth_rack.svg" width="180"/></td>
<td>Real-time audio through SDL3, in the browser too: an arpeggiator, a filter and a delay on an
audio thread that takes no lock and allocates nothing. Settings reach it through a lock-free
triple buffer, samples come back through a ring, and an analyzer thread publishes the spectrum
with <code>Shared&lt;T&gt;</code></td>
</tr>
<tr>
<td><a href="model_data_studio/">model_data_studio</a></td>
<td><img src="../doc/screenshots/data_studio.svg" width="180"/></td>
<td>A SQL client over an in-memory SQLite database of 500,000 rows. Queries run on a worker
thread and can be cancelled; the result goes to a table whose columns follow the query, and to a
chart</td>
</tr>
<tr>
<td><a href="model_task_runner/">model_task_runner</a></td>
<td><img src="../doc/screenshots/task_runner.svg" width="180"/></td>
<td>A build panel: real processes on the desktop, generated runs everywhere, streamed through a
<code>Channel&lt;T&gt;</code> in batches into consoles that follow their newest line. Rows are
drawn by a custom widget, a specialization of <code>Widget&lt;T&gt;</code></td>
</tr>
</table>

`showcase/` is a separate, internal set of minimal snippets used only to auto-generate the
small feature screenshots embedded in the top-level README — not meant to be read as usage
examples on their own.

Python counterparts live in [`../python/examples/`](../python/examples/README.md) — same ideas via `prism.Model` (`01_counter`, `02_mixer`, `03_validation_and_transaction`, `04_background_shared_channel`, `05_lists_and_derived`). Run with `PYTHONPATH=builddir/python python python/examples/01_counter.py`.

## Running

Every example is built with the library:

```bash
meson setup builddir
ninja -C builddir
./builddir/examples/widget_gallery/widget_gallery
```

Each also writes a one-frame SVG capture when given an output path, which is how the screenshots
above are made (`scripts/update_screenshots.sh` refreshes them all):

```bash
./builddir/examples/widget_gallery/widget_gallery capture.svg 2   # the gallery's third tab
```

Meson's `custom_target` doesn't track header dependencies, so if you change a header without
touching the `.cpp` that includes it, delete the stale `.svg` under `builddir/` before rebuilding
or ninja won't notice it needs regenerating.

For the browser build, see [In the browser](../README.md#in-the-browser) in the top-level README;
`scripts/build_web_site.sh` assembles the demo page.
