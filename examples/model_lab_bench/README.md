# model_lab_bench Example

A bench instrument: an oscilloscope, a DC power supply and a function generator, driven by a
simulated USB instrument on its own thread. Its two stall buttons are the point of the
example: one blocks the hardware thread, the other the thread that owns the model.

<p align="center"><img src="../../doc/screenshots/lab_bench.svg" alt="model_lab_bench screenshot" width="700"/></p>

## Try this

1. Open **Stress & Fault Injection** and press **Stall the hardware thread**.
2. Drag the voltage slider during the two seconds. It moves, its value and the
   "Max power" line follow, and the status bar's frame age climbs to 2 s.
3. Press **Stall the logic thread**. Now the window stops answering: a click waits
   until the stall ends. That is every GUI toolkit whose widgets and model share one thread.

In PRISM a slow device, a slow file or a slow network call blocks only the thread that waits
for it. The window keeps drawing and stays interactive, and shows the last frame it got.

## How it is built

- **Controls are fields.** Each slider is a `Field<ui::Slider<double>>` with a `label` and a
  `unit`, so it prints its own value. The waveform is a `Field<WaveformType>`, an enum: its
  dropdown comes free.
- **Readouts are `Derived`.** "Max power", the period and the supply's CC/CV mode are
  computed from the controls and the last frame, and recompute when those change.
- **One value crosses to the hardware thread.** A `Derived<DaqSettings>` gathers every setpoint
  on the logic thread; its observer hands it to a `Shared<DaqSettings>` the worker reads. The
  worker never touches a `Field`.
- **Frames come back coalesced, events in order.** Frames travel in a `Shared<DaqTelemetry>`:
  only the newest one matters. State changes the instrument sees (protection tripped,
  constant-current mode) travel in a `Channel<DaqEvent>`: none is dropped. The model's
  `drain()` is where both cross over, on the logic thread.
- **The scope is a canvas.** `canvas()` draws the frame with a 1-2-5 autoscale and prints the
  scale it drew. `depends_on(frame)` repaints it when a frame arrives, and only then.
- **The instrument is pure.** `lab_bench_model.hpp` (waveforms, the supply's load line,
  autoscale, measurements) has no threads and no IO; `tests/test_lab_bench.cpp` tests it.

The Python counterpart is `python/examples/16_lab_instrument.py`.
