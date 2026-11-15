# Review — `examples/model_lab_bench/` and `python/examples/16_lab_instrument.py`

> **Status: addressed.** This is the review as written, kept for the record. The example-only
> rewrite that followed it (no framework changes) removed every P0/P1 finding in it: the
> cross-thread `Field` reads are gone (`Derived<DaqSettings>` → `Shared<DaqSettings>`), the
> self-mutating button observer with it, the `Channel` now carries real transitions from the
> hardware thread, the hand-rolled enum layer was replaced by `Field<WaveformType>`, the scope
> draws an absolute 2 ms sweep with trigger alignment, a 1-2-5 autoscale and rulers printed from
> the values actually drawn, and the Python counterpart has a real plot, a `main()` and tests.
> Read the sections below as the argument for what changed, not as a description of the code.
> Later the same day: the bench got the `drain()` its view-driven model needs (without it no frame
> crossed over, natively or on the web), a one-shot stall request (an atomic exchange, not a
> read-then-clear on a `Shared<bool>`), and framework gap 2 below: `Slider` now has a `label` and
> a `unit` and prints its own value, which removed six caption `Derived`s.

Scope: the lab-bench demo (C++ example, its pure model header, its SVG web card, its
Python counterpart, its tests) reviewed against PRISM's three stated claims — *write a
struct get a UI*, *stalled work never freezes the frame*, *idiomatic modern C++26*. All
findings below were reproduced on `builddir` (GCC 16.2.1, `-freflection` on), not inferred
from reading.

Everything reviewed is uncommitted (`examples/model_lab_bench/`,
`python/examples/16_lab_instrument.py`, `tests/test_lab_bench.cpp`,
`doc/screenshots/lab_bench.svg` are untracked).

---

## Verdict

The example is *competent code that argues against the framework it lives in*. It builds,
it renders, its unit tests pass, and it uses `Shared<T>`, `std::jthread`, strong geometry
types and the dock DSL correctly. But on the two claims that matter — "reflection removes
the glue" and "the logic thread can't freeze the UI" — it does the opposite: it hand-wires
~15 fields, ~10 lambdas, a manual repaint token and a hand-written enum→string table, and
it races the UI thread. The decoupling demo is also aimed at the wrong thread (see P0-4).

| Axis | Grade | One-line reason |
|---|---|---|
| 1. Idiomatic modern C++26 | C+ | Good types and no raw pointers, but three levels of `[&]` lambdas, `[=]` implicit-`this` captures, and a mortal 75-line math function |
| 2. Exploits PRISM | D | Zero `Derived`, zero `transaction`, dead `Channel`, reflection unused, `ui::Choice` reinvents the enum layer |
| 3. Decoupling proof | D | 7 racy `Field` reads off the logic thread (UB, TSan-proven) and a stall that no realistic GUI would have frozen anyway |
| 4. Instrument credibility | D+ | Every printed scale factor is wrong (4.46 V/div drawn vs "2.00V/div" printed); the frequency slider is visually a no-op |
| 5. Ergonomics / polish | D | No visible label on any control, no numeric readout, 58% of the scope pane empty, no persistence of setpoints |
| Tests | B− | Good pure-logic split, real gaps, and structurally unable to catch the race |

---

## P0 — correctness

### P0-1. Seven data races on `Field::get()` from the worker thread (undefined behaviour)

`model_lab_bench.cpp:304-311`:

```cpp
s.target_voltage = app.target_voltage.get().value;
s.current_limit  = app.current_limit.get().value;
s.output_enabled = app.output_toggle.get().checked.value_or(false);
s.wave_type      = static_cast<WaveformType>(app.wave_choice.get().selected.value_or(0));
s.frequency_hz   = app.frequency.get().value;
s.amplitude_v    = app.amplitude.get().value;
s.offset_v       = app.offset.get().value;
```

`ObservableValue::get()` (`include/prism/core/field.hpp:27`) is an unsynchronised read of
the value; `set()` asserts `may_change_model()` and would abort if *called* from this
thread, but the framework's own contract (`include/prism/core/logic_thread.hpp`) says the
model is touched only on the logic thread. The worker therefore reads the model while the
UI thread writes it from every slider drag — a race the framework merely fails to *assert*
on, not one it permits.

Proof (`g++ -std=c++26 -fsanitize=thread`, two threads, same shape: `acc += f.get()` vs
`f.set(f.get() + 0.5)`):

```
WARNING: ThreadSanitizer: data race
  Write of size 8 by main thread at prism/core/field.hpp:27
  Previous read of size 8 by thread T1 at prism/core/field.hpp:27
SUMMARY: ThreadSanitizer: data race in ObservableValue<double,Field<double>>::set
```

The TSan CI job cannot catch this: it excludes `model_app`
(`.github/workflows/ci.yml:160`), which is the only harness this example can run under.

**Fix.** The UI→worker direction must go through a coalescing cell, exactly like
`examples/perf_lab/` does for the reverse direction:

```cpp
core::Shared<DaqSettings> settings_ingest{};   // UI -> worker, latest-wins
core::Derived<DaqSettings> settings{[this] {
    return DaqSettings{.target_voltage = target_voltage.get().value, /* ... */};
}, target_voltage, current_limit, output_toggle, wave_choice, frequency, amplitude, offset};
```

then `settings.on_change().connect([&](const DaqSettings& s){ settings_ingest.set(s); })` in
the model — one instance, no `seed_demo_data()` copy-paste of the same seven lines
(`model_lab_bench.cpp:209-215` duplicates that block verbatim), and the worker reads only
`settings_ingest.get()`. `std::atomic<bool> trigger_lag/force_fault` then disappear too:
both are already expressible as `DaqSettings` members (`simulate_fault` exists in the model
struct and is *never used by the UI*, which grew a second flag instead).

### P0-2. The output toggle mutates its own field inside its own observer

`model_lab_bench.cpp:78-81`:

```cpp
wiring.push_back(output_toggle.on_change().connect([this](const ui::Button& b) {
    output_toggle.set({.text = b.checked ? "Output: ON" : "Output: OFF", .checked = b.checked});
    push_event(b.checked ? "DC Output Enabled" : "DC Output Disabled", false);
}));
```

`SenderHub::emit` (`include/prism/core/connection.hpp`) snapshots the callback list and
calls it, with no re-entrancy guard — so the inner `set()` re-enters the same observer.
Reproduced with the same struct shape:

```
handler ran 2 times for one click; text='Output: ON' click_count=0
```

Consequences, in the shipped demo: **two identical lines in the event log per toggle**, and
`click_count` reset to 0 every time (breaking any code that counts toggles). It only
terminates because the second pass happens to reach value equality.

**Fix.** The button's caption is not data, it is a view of data:

```cpp
core::Field<bool> output_enabled{true};
core::Derived<std::string> output_caption{[this] {
    return output_enabled.get() ? "Output: ON" : "Output: OFF"; }, output_enabled};
```

and bind both widgets to it, or — simplest — drop the caption entirely and let
`Widget<Button>` render a checked/unchecked toggle whose state *is* the model.

### P0-3. `event_ingest` is dead, and the docs say otherwise

`core::Channel<DaqEvent> event_ingest` (`:53`) is declared, given a waker (`:286`) and
observed (`:289`) — **nothing anywhere calls `.send()`**. `DaqEvent::timestamp` is never
assigned by any code path. Every event is pushed synchronously on the logic thread by
`push_event` (`:105`), so `ingest_event` is unreachable in practice.

That is a shame twice over: (a) the channel is exactly the right primitive for
"worker observes a fault → ordered, lossless event in the log", which is the most
demonstrative line the demo could contain; (b) the web card and the Python docstring both
advertise "lossless hardware events", which today is untrue. Unused-but-wired plumbing is
worse than absent plumbing: a reader concludes the example demonstrates `Channel` when it
demonstrates `List::push_back`.

**Fix.** Move event generation into the worker (`Over-current trip`, `CC/CV mode change`,
`Output enabled`), stamp `std::chrono`-formatted timestamps, `event_ingest.send(...)`, and
delete `push_event` from every UI-thread handler. ~15 lines, and the demo finally shows
*ordering guarantees across the thread boundary* — a claim no competing toy framework in
this space can make.

### P0-4. The decoupling demo proves something no framework fails at

`inject_lag_btn` sets `trigger_lag` (`:84-87`), and the worker sleeps 2000 ms (`:295-297`).
The consequent freeze is invisible: the worker is a background thread, and blocking a
background thread has never frozen a GUI in any toolkit. **Every UI framework in
existence passes this test.** What PRISM uniquely claims is that a stalled *logic thread*
does not stop presentation, input or resize — because presentation runs elsewhere. The demo
never shows that, so a skeptical reader sees a 2 s `sleep_for` and concludes nothing.

Worse, the stall is not even observable while it happens: no latency readout, no heartbeat,
no "last tick 1.8 s ago" indicator, and the fault injection button's only feedback is a log
line that the user's own click already implies.

Finally, shutdown blocks: the 2 s `sleep_for` (`:297`, `:316`) ignores `stop`, so
`~jthread` joins for up to two seconds after the window closes — a 2 s hang on quit is a
bug report waiting to happen.

**Fix — make the A/B explicit.** Keep one toggle "run the poll loop on the logic thread"
(the naive design every other framework forces) and one "on a worker" (PRISM):

```cpp
if (app.on_logic_thread.exchange(false)) {
    std::this_thread::sleep_for(2000ms);   // naive: this freezes everything
}
```

with the poll loop itself running through `ctx.post(...)` in that mode, plus a
`Derived<std::string>` heartbeat showing worker latency in ms. Then dragging the window,
resizing the dock and moving a slider during the 2 s stall *is* the demo — the difference
is visible in a two-second screen recording, which is the only review a framework like this
actually gets. And use stop-aware waiting
(`std::stop_token`/`condition_variable_any::wait_for`) or a cooperative deadline so quitting
is instant.

---

## P1 — the example argues against its own framework

### P1-1. `ui::Choice` + a hand-written enum table reinvents machinery that already exists

`wave_choice` (`:40`) is a `Field<ui::Choice>` with `options = {"Sine","Square",
"Triangle","Sawtooth","Noise"}`, complemented by `waveform_name()` (`lab_bench_model.hpp:25`)
and `parse_waveform()` (`:36`), then converted back with
`static_cast<WaveformType>(selected.value_or(0))` at `:187` and again at `:212` and `:307`.

PRISM already has the whole layer: `template <ScopedEnum T> struct Widget<T>`
(`include/prism/ui/delegate.hpp:1134`) backed by `enum_count/enum_label/enum_index/
enum_from_index`, reflective under `-freflection` and via magic_enum otherwise
(`delegate.hpp:195-265`). `core::Field<WaveformType>` renders a dropdown with the
enumerator names for free; there is no `options` list to keep in sync, no `parse_waveform`,
no `static_cast` at three call sites, and no silent mislabelling when someone reorders the
enum or the options vector. `parse_waveform` is dead outside the unit test that exercises it
— a smell that the abstraction is being tested rather than used.

Also note the two spellings of the same truth: `wave_choice.get().selected.value_or(0)`
appears in the canvas (`:187`) and in both `DaqSettings` builders (`:212`, `:307`). Three
places to update when the set of waveforms changes.

### P1-2. No `Derived` anywhere, in a file whose first lambda is a hand-rolled one

`model_lab_bench.cpp:70-75`:

```cpp
auto update_psu_calc = [this] { /* fmt::format the product of two fields */ };
wiring.push_back(target_voltage.on_change().connect([=](const auto&) { update_psu_calc(); }));
wiring.push_back(current_limit.on_change().connect([=](const auto&) { update_psu_calc(); }));
```

That is a `Derived` with the cache, the invalidation and the two subscriptions written by
hand. `core::Derived<T>` exists (`include/prism/core/derived.hpp`), and
`vb.widget(Derived<T>&)` renders it (`view_builder.hpp:528`) — the replacement is shorter
than what it replaces:

```cpp
core::Derived<std::string> psu_calc{[this] {
    return fmt::format("Calculated Max Power: {:.1f} W",
                       target_voltage.get().value * current_limit.get().value);
}, target_voltage, current_limit};
```

`status_telemetry`, `status_connection` and even `sim_connected` are the same story:
`status_telemetry` is a `Field<Label<>>` written from `ingest_telemetry` (`:114-124`), i.e.
a derived display string stored as authoritative state; `status_connection` is a static lie
("Status: Connected (USB 2.0 TMC)") while `sim_connected` — a checkbox the user can untick —
is wired to *nothing*. Three fields, zero behaviour, all of them `Derived` in disguise.

`[=]` capturing a `[this]`-capturing lambda (`:74-75`) is also deprecated since C++20 —
`-Wdeprecated` should fire; check the warning flags for the examples lane.

### P1-3. `telemetry_rev` is a manual repaint token that duplicates the observer graph

`:57` declares `Field<uint64_t> telemetry_rev{0}` with the comment "triggers scope canvas
repaint"; `:114` does `telemetry_rev.set(telemetry_rev.get() + 1)`; `:218` (seed path) does
`telemetry_rev.set(1)` — two inconsistent conventions for the same counter; `:226` hangs
the canvas off `.depends_on(telemetry_rev)`.

`core::Shared<T>` is already an observable (`shared.hpp`: `observe`, `on_change`), and
`vb.widget(Shared<T>&)` even wires `drain_fn` and `bind_waker_fn` for you
(`view_builder.hpp:537`). `p.canvas(*this).depends_on(telemetry_ingest)` expresses the real
dependency; `latest_telemetry` stays as the paint-time cache. The counter then has no
remaining consumer and should be deleted — a monotonic counter used only for `depends_on`
is a symptom of the canvas not knowing where its data lives.

### P1-4. No transactional batching, in the tick path that exists to be coalesced

`ingest_telemetry` (`:111-124`) sets `telemetry_rev`, then `status_telemetry`, on every
40 Hz tick, and each `set` walks the observer graph with a mutex + heap-allocated callback
snapshot per emit (`connection.hpp`). `core::transaction(fn)` /
`TransactionGuard` (`include/prism/core/transaction.hpp`, coalescing per sender in waves)
exists precisely for this, and **no example in the repo uses it** — grep over `examples/`
returns nothing, which is why nobody has found the rough edges. This example is the natural
first user: one `transaction([&]{...})` around the per-tick mutation set is one line, fewer
emits, and one fewer thing for a reader to wonder about.

(A transaction guard would also have made P0-2's double-emit harmless rather than merely
terminating.)

---

## P1 — the oscilloscope is a mock, and the numbers prove it

Rendered at 1:1 from `builddir/examples/model_lab_bench/lab_bench.svg`.

### The printed scale factors are wrong

The canvas draws `py = cy - (sample / 15.0) * (h * 0.42)` (`:174-175`) over an 8-division
grid (`:151`). At the captured `h = 220`: `dy = 27.5 px`, half-scale `= 92.4 px` for
±15 V ⇒ **4.46 V/div drawn**, while the header (`:186`) prints `"2.00V/div"` — a 2.2×
lie, in the one place a scope user reads first.

Horizontally it is worse: the buffer always holds exactly `periods_shown = 3.0` periods
(`lab_bench_model.hpp:140`), while the label claims `250us/div` × 10 div = 2.5 ms — off by
1.2× at 1 kHz and by 16.7× at the 20 kHz the frequency slider allows. **Consequence:
dragging the frequency slider cannot change the picture.** The buffer is rebuilt with three
periods of whatever frequency is set, so the trace looks identical at 50 Hz and 20 kHz;
only the starting phase (`time_sec * freq`) moves, and at 25 ms × 1 kHz = 25 exact cycles it
does not even visibly move. The single most inviting control in the panel is a visual no-op,
and a reviewer who drags it will conclude the whole canvas is animating garbage.

### Missing everything that makes a trace an instrument

- No auto-range: full scale is a hardcoded 15 V while the UI can only produce ≤ 10 V
  amplitude + 4 V offset, so the trace is pinned to ~⅓ of the screen height and
  `clamp(..., -1, 1)` silently flattens anything larger.
- No per-division labels, no axis values, no units on the grid.
- No trigger indicator on the trace (the word "TRIG AUTO" appears, unpinned to anything).
- No cursors, no measurement gates — the Vpp/Vrms/Freq footer is computed in the model
  (`:164`) rather than measured off-screen, so it can never disagree with the trace, which
  is exactly what the demo of a *canvas* should get to show.
- No persistence/afterglow despite the "phosphor" comment at `:134`.
- No channel indicator, no 0 V reference marker.
- Only `node.theme->border` is themed (`:203`); every other colour is a literal
  (`rgba(18,22,26)`, `rgba(35,48,54)`, `rgba(0,255,170,70)`...), so the canvas ignores light
  mode, high contrast and every other theme in the repo.

### Two real defects in the paint path

- `:167` `float step_x = w / static_cast<float>(samples.size() - 1);` — `size() == 1`
  yields `w / 0` ⇒ `inf`, then `0 * inf = NaN` in the reported position. `!samples.empty()`
  is the only guard. One-sample buffers happen the moment someone seeds a demo or a
  one-shot capture.
- `:165-172` allocates `std::vector<core::Point> pts` and fills it on every paint — 240
  elements at 60 fps, per canvas, per frame, in a framework whose pitch is a
  dirty-repainting persistent tree. `DrawList` has no "polyline from a float buffer"
  primitive, so the allocation is papering over a missing `dl.polyline_xy(span<float>,
  transform, ...)`; that is worth filing, but the example should at least reuse a member
  buffer rather than heap-allocating in `record`.

### What a credible scope looks like here

```cpp
struct ScopeFrame {                    // what the worker actually produces
    std::array<float, 240> samples;    // fixed, copy-cheap, no vector in the hot path
    float v_per_div;                   // computed by the producer...
    float s_per_div;                   // ...so the header cannot lie
    bool  fault;
};

// consumer: derive the scale from the data, then print what was drawn
auto vpp    = std::ranges::max(samples) - std::ranges::min(samples);
auto v_div  = nice_step(vpp / 6.f);              // 1-2-5 decade ladder
auto s_div  = nice_step(window_seconds / 10.f);
dl.text(fmt::format("CH1: {:.2f}V/div | {:.0f}us/div | TRIG {}", v_div, s_div*1e6, mode), ...);
```

Reusing one member `std::array<Point, 240>` kills the per-paint allocation; the 1-2-5
ladder (`{1,2,5} × 10^n`) is the whole of "auto-range" for an oscilloscope and is ten lines.
Fixing the producer to emit a fixed window (say 5 ms) instead of "3 periods" makes the
frequency slider *do* something, which is the single cheapest credibility win available.

---

## P1 — layout and ergonomics

### `min_size(Height{220})` turns the scope into a strip

`model_lab_bench.cpp:226`:
`.canvas(*this).depends_on(telemetry_rev).min_size(core::Width{300}).min_size(core::Height{220})`

`measure_node` for `Kind::Canvas` (`include/prism/ui/layout.hpp:262`) reads: *if a min size
is set → `preferred = min_size, expand = false`; otherwise `preferred = 0, expand = true`*.
So asking for a *minimum* height pins the canvas to **exactly** 220 px and opts it out of
expansion. In the rendered card that leaves the trace in a 524×220 strip at the top of a
pane with roughly 58% empty dark space beneath it — the demo's centrepiece looks broken in
the one screenshot that gets published.

Two things to fix, and the second is the framework's:

1. Delete both `min_size` calls; the canvas fills its pane by default, which is what a
   scope should do.
2. `min_size()` is a misleading name for "fixed, non-expanding" — a reader will reach for
   it precisely because they want a floor, and get a nail. Either honour the name
   (`expand = true`, floor only) or split into `preferred_size()`/`fixed_size()`. Every
   example that uses it is now a trap for the next author. File this; it is a one-line
   layout change plus a test in `test_layout.cpp`.

### No control has a visible label

In the render: two sliders, both anonymous thin tracks with thumbs; a full-width blue
`Output: ON`; a "Calculated Max Power: 24.0 W" line floating with nothing near it. A user
cannot tell which slider is voltage and which is current limit, and cannot read either
value to any precision.

Root cause is partly framework: `Slider<T,O>{value,min,max,step}` has no caption and no
value readout (`delegate.hpp:825`), and although the reflection walk fills `Node::name`
(`view_builder.hpp:288`, the *accessible* name), **`node.name` is never drawn anywhere** —
grep it: there is no paint site. So PRISM today cannot label a controlled widget in any
example; `widget_gallery/controls_tab.hpp` shows the same unlabelled slider. That is worth
saying out loud in the review of a flagship UI example:

```cpp
p.hstack([&]{ p.widget(Field<Label<>>{"Target Voltage"}).width(...);
              p.widget(target_voltage);
              p.widget(Derived<std::string>{...}); });   // 12.00 V
```

is the shape that should be a *default*, not boilerplate every example re-derives. Either
`Widget<Slider>` grows a caption + readout slot, or `hstack`/`form_row` gets a labelled-row
helper. The lab bench is the forcing function that proves it's needed.

Same category, cheaper to fix inside the example:

- The status bar says "Status: Connected (USB 2.0 TMC)" unconditionally, next to a
  `sim_connected` checkbox that does nothing. Unticking it should flip that string (a
  `Derived`) and grey out the controls (`enabled_if`, which exists).
- The flagship screenshot shows "DC Power Supply" with "Waveform Generator" *hidden behind
  a tab*. The single most visual control in the app is invisible in its own showcase. Drop
  the tab pair in the right column, or make the default dock layout a `vstack` of both
  sections instead of a `dock_split`. Related: a two-tab `dock_group({"power_supply",
  "wave_gen"})` is a tab bar wearing a dock's clothes; `vb.tabs()` is the honest primitive.
- Event log lines have no timestamps, in an app whose whole subject is ordering.
- No menu, no shortcuts (`Button::shortcut` exists), no Reset-to-default, and — the real
  miss — **no persistence of the instrument setpoints**. `LabBenchPrefs` (`:26-30`) holds
  only `layout` and `window`, while `app::SettingsFile` is instantiated (`:279`) and saves
  exactly those two things. A real bench remembers 12.0 V / 2.0 A / 1 kHz across runs; the
  demo silently forgets, which reads as "the framework's settings story is half-built".
  Putting `DaqSettings` in prefs fixes it and demonstrates `SettingsFile` on reflected data.

---

## P2 — the model layer

### `synthesize_telemetry` is 75 lines at three abstraction levels

`lab_bench_model.hpp:92-168` does, in order, with no named helpers: CV/CC operating-point
selection, a thermal model, waveform synthesis, noise generation, and Vpp/Vrms statistics.
The repo's own rule ("a function should operate at one level of abstraction"; extract
`detail::` free functions rather than comment-decorate blocks) is violated exactly where it
is easiest to satisfy — `operating_point()`, `sample_window()`, `measure()`, each 10-20
lines, pure, and independently testable.

### Four real defects in it

1. **Divergent duplicate defaults.** `DaqSettings` defaults 5.0 V / 1.5 A / ±5 V / 10 Hz–50 kHz
   (`:44-53`), while the UI initialises 12.0 V / 2.0 A / ±4 V / 50 Hz–20 kHz. Two sources of
   truth for the same instrument; the screenshot's "12.00 V" and the model's "5.0 V" disagree
   the moment anyone uses the model without the UI (which `seed_demo_data` then papers over).
2. **`mt19937` per frame, 200 normal draws even when not needed.** `:145-146` constructs a
   fresh 2.5 KB `std::mt19937` each call, then `:151-160` draws a `normal_distribution`
   sample *for every sample, for every waveform*, discarding it unless the type is `Noise`.
   At 40 Hz that is 8 000 wasted Gaussian draws per second, plus a reseed per frame.
3. **No `simplify:` comments** for any of the deliberate cuts: the fixed 3-period window
   (`:140`), the 15 V full scale, the resistor-only load model, the 150-line log cap
   (`:105-109`). The repo mandates these ("a deliberate cut gets a `simplify:` comment
   naming the ceiling and the upgrade path") — and a simulation is where they matter most,
   because the next reader cannot tell a simplification from a bug.
4. **`DaqTelemetry` mixes millisecond-scale scalars with a 200-float vector**, so every tick
   copies that vector 3-4 times: worker temp → `make_shared<const T>` in the atomic cell →
   `Shared::get()` copy into `latest_telemetry` → `DaqSettings` copy for the seed path.
   `std::array<float,240>` + a separate `Shared<ScopeFrame>` (or `move`-friendly
   `Shared<std::shared_ptr<const Frame>>`) removes all of it. This is also the reason
   `synthesize_telemetry` can't be trivially constexpr/noexcept.

### One readout is real, one is an echo, and one field name lies

The math checks out where it counts, and that is worth saying: `amp = amplitude_v * 0.5`
(`:140`) treats `amplitude_v` as peak-to-peak, so a sine gives `Vrms = 2.5/√2 = 1.768`,
which is exactly the "Vrms: 1.77V" the screenshot shows. (This reviewer checked it against a
textbook and *expected* it to be wrong — because the field is called `amplitude_v` while
meaning Vpp. Two names, one number: rename it `amplitude_vpp` and the doubt disappears.)

Two things do undermine the readouts:

- `t.measured_freq = s.frequency_hz` (`:165`) — the "Freq" in the footer is the setpoint
  echoed back, not measured from the buffer. It can never disagree with the slider, so it
  demonstrates nothing, and it makes the neighbouring *genuinely* computed Vpp/Vrms look
  equally fake. Measure it (zero-crossings over the buffer, ten lines) or drop it.
- Vpp is a min/max over 200 discrete points, so any waveform whose peak falls between
  samples under-reports (visibly, on triangle and square at high frequencies). Measuring
  the buffer the canvas actually draws — as suggested above — makes the footer
  self-consistent by construction, which is precisely the property an "instrument" demo
  should show off.

---

## P2 — tests

`tests/test_lab_bench.cpp`: 6 cases / 40 assertions, passing. The shape is right — pure
logic, no threads, no IO, following `tests/test_proc_metrics.cpp`. Gaps:

- No `Noise` waveform case (the only branch with state, and the noisiest numerically).
- No Vpp/Vrms case pinning the peak-to-peak semantics of `amplitude_v` (see P2).
- No `waveform_name`/`parse_waveform` round-trip (and the fact that this is testable-but-
  unused is a hint the pair shouldn't exist — see P1-1).
- No `sample_count == 0` / `== 1` edge cases, i.e. nothing guards the `w/size-1` division
  in the canvas or `sum_sq/sample_count` in the model.
- **No test can catch P0-1**, because the only harness that exercises the racy path is
  `model_app`, which CI excludes from the TSan job. A four-line pure test — one thread
  `set()`s, one `get()`s, run under `-Db_sanitize=thread` — would have flagged the exact
  access pattern before it shipped, and would keep flagging it for every future example.
  Given the framework's central claim is thread decoupling, *that* test is the one missing
  piece of infrastructure here.

---

## P2 — the Python counterpart is a different, weaker demo

`python/examples/16_lab_instrument.py` is not a port; it is a second, smaller app that
shares a name. Divergences, in order of how much they hurt:

- **No scope.** It cannot have one: `vb.canvas` in Python accepts plot handles only
  (`plot_field()` / `py_canvas_dispatch`), and there is no custom-draw canvas binding. So
  the C++ demo's centrepiece is simply absent, and the Python reader is told the framework
  has no custom canvas. (If a plot-based equivalent is acceptable, `plot_field()` with the
  synthesized samples would at least show *something*; the honest fix is a
  `canvas(draw_fn)` binding, since the C++ API exists.)
- **No `main()`.** Every other example in `python/examples/` follows the
  `_load_example(name)` + `mod.main()` pattern in `python/tests/test_prism_python.py:3547`,
  which means this file is **untested** — the only example in the directory that is.
- **`prism.derived(...)` is used, but with string dependency names**, which the binding's
  own docstring calls the fallback ("descriptor deps are preferred — a typo is a NameError
  at class-body time, not silent"). The only Python example using the flagship reactive
  primitive demonstrates the discouraged form.
- **The waveform choice has 4 options; C++ has 5** (`Sawtooth` missing), so the two demos
  disagree about the model's own enum.
- **The button text never updates** — it reads "Output: ON" while off, the Python
  analogue of P0-2 but with no re-entrant set to accidentally terminate it.
- **The channel receives only a random 5% "calibration OK" line** from nowhere in
  particular, so the docstring's "lossless hardware events" is, again, advertising for
  behaviour that isn't there.

Either bring it to parity (plot-based scope, `main()`, a live test, descriptor-derived
fields, 5 waveforms) or cut it and say the Python bindings don't yet cover custom canvases.
What shouldn't ship is a second example that makes the framework look smaller than it is.

---

## P3 — docs and packaging

- `README.md`'s example list (~line 628) and `examples/README.md`'s gallery table don't
  mention `model_lab_bench` at all — the newest and most visually striking example is
  invisible from both entry points.
- `doc/screenshots/lab_bench.svg` is required by `scripts/build_web_site.sh`
  (`cp "doc/screenshots/$shot.svg"`) but is untracked; the script also copies
  `$name.{html,js,wasm,data}`, and `builddir-wasm` predates this example — the wasm lane
  has not been shown to emit `model_lab_bench.data`. Worth proving before the page goes up,
  since a missing `.data` fails at load, not at build.
- No `simplify:` comments (P2), and no note anywhere that the "hardware" is synthetic —
  the status bar claims USB-TMC on `/dev/usbtmc0`, which a reader may take literally.

---

## The two framework gaps this example exposes (highest leverage)

Worth stating separately, because they are why the example looks hand-wired, and because
they outlive it:

1. **A numeric member of a reflected struct renders as read-only text, not a control.**
   `template <Numeric T> struct Widget<T>` (`delegate.hpp:497`) is a `Role::Label` whose
   `handle_input` is a no-op. So "write a struct, get a UI" delivers a *monitor*: `bool` →
   checkbox, scoped enum → dropdown, `double` → text that can't be edited. To build an
   instrument panel you must hand-declare `Field<ui::Slider<double>>` for every numeric —
   which is exactly what this example does, and exactly why it looks like the framework's
   pitch failed. The missing piece is one annotation:
   `[[=prism::core::range<0.0, 30.0, 0.1>]] double target_voltage = 12.0;` → `Slider<double>`
   (with `[[=label<...>]]`/`[[=section<...>]]` already existing to caption it). That single
   annotation turns this 322-line example into something near 120 lines, and it is the most
   valuable thing a lab-bench demo could push the framework to grow.
2. **No painted caption for a controlled widget.** `Node::name` is written by the reflection
   walk and never drawn; `Slider` has no caption slot. Every example therefore ships
   anonymous sliders. A `Widget<Slider>` caption/readout slot, or a `form_row(label,
   field)` helper in `ViewBuilder`, is small and improves every existing example at once.

---

## The rewrite, concretely

The shape I'd want the demo to have, in order of payoff per line:

```cpp
// 1. Control state: one struct, one Derived, one Shared. Kills 7 racy reads (P0-1),
//    the seed_demo_data copy-paste, and both atomics.
core::Derived<DaqSettings> settings_from_ui{[this] { /* 7 reads, on the logic thread */ },
                                            target_voltage, current_limit, output_enabled,
                                            wave_type, frequency, amplitude, offset};
core::Shared<DaqSettings> settings_ingest{};
// 2. Waveform type is an enum field: free dropdown, no options list, no cast.
core::Field<WaveformType> wave_type{WaveformType::Sine};
// 3. Computed strings are Derived, not stored state.
core::Derived<std::string> psu_calc{...}, status_line{...}, output_caption{...};
// 4. Worker: reads settings_ingest.get(), sends events, emits Shared<ScopeFrame>
//    (std::array, v_per_div, s_per_div). Stop-aware waits.
// 5. Canvas: depends_on(scope_frame), auto-ranged 1-2-5, prints the scale it drew,
//    member buffer reused, no min_size.
// 6. ingest: one transaction([&]{...}) per tick.
// 7. Python: plot-based scope or a canvas binding, main(), live test, 5 waveforms.
```

Cheapest first: delete the two `min_size` calls (1 line), delete the `waveform_name`
table and `ui::Choice` (~30 lines), replace `telemetry_rev` with `depends_on(telemetry_ingest)`
(~3 lines), route settings through a `Shared<DaqSettings>` (~15 lines, fixes UB). That is
under an hour and moves the example from "argues against the framework" to "shows the
framework".

---

## Credit where it's due

- The pure model header (`lab_bench_model.hpp`) has no threading, no IO, no SDL — exactly
  the repo's "keep pure logic separable" rule, and it has a real doctest file.
- `Shared<T>` + `set_waker(ctx.waker())` + `observe` on the logic thread is the *correct*
  UI-side half of the pattern; only the worker half breaks the contract.
- Keeping every `Connection` in `wiring` correctly avoids the `[[nodiscard]]` + destructor-
  disconnects trap that `connect()`'s API sets for the unwary — a subtlety most authors
  get wrong.
- Strong `X/Y/DX/DY/Width/Height` scalars throughout the canvas, designated initializers,
  `std::jthread`, no raw pointers, SDL types never leak.
- Docking with a persisted `DockLayout` default, custom window decoration, remembered
  geometry, tooltips on every control, and a headless SVG capture wired into meson and the
  web-page builder — the packaging discipline is better than the average example.
