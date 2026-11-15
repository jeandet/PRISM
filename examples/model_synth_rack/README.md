# model_synth_rack Example

A small synthesizer: an arpeggiator, a resonant filter, a feedback delay, and a spectrum
analyzer that draws what comes out. Its audio callback runs on a thread of its own. Press
**Power** and it plays; the numbers under **Output & Callback Budget** are what that callback
actually costs, measured on the machine you are looking at.

<p align="center"><img src="../../doc/screenshots/synth_rack.svg" alt="model_synth_rack screenshot" width="700"/></p>

## Try this

1. Press **Power**, then **Stall the logic thread (2 s)**. The window stops answering for two
   seconds. The sound does not stop for a sample, and the analyzer is drawing again the moment
   the window comes back.
2. Drag **Cutoff** and **Resonance** while it plays. Each block renders from one settings
   snapshot, so a control moving mid-block can never tear the render into a crackle.
3. Switch the pattern to **UpDown** or **Random**, change **Tempo**, and uncheck a few of the
   sixteen step boxes. The analyzer's step strip follows the beat.
4. Read **callback … ms mean · … ms max**: that is the whole voice chain on one core, with most
   of the 10.67 ms block budget still free.

## How it is built

- **Controls are fields.** Every slider is a `Field<ui::Slider<double>>` carrying its own label
  and step (and a unit where the value has one), so it prints its value and snaps to its step on
  its own. The waveform, chord and pattern are enums, so their dropdowns come free, and the
  sixteen step gates are `Field<ui::Checkbox>`.
- **One value crosses to the audio thread.** A `Derived<SynthSettings>` gathers every control on
  the logic thread and publishes a trivially copyable snapshot into an `RtLatest`, a lock-free
  triple buffer. The callback reads the newest snapshot at the top of each block. PRISM's usual
  handoff, `core::Shared<T>`, allocates on `set()`, which an audio callback must not do.
- **Samples come back in a ring, display frames in a `Shared`.** The callback writes its block
  into a preallocated `SampleRing` of relaxed atomics, so the reader can never race the writer. A
  second thread takes the newest 2048 samples thirty times a second, runs the FFT, and publishes
  one `AnalyzerFrame` through a `core::Shared`; the model drains it on the logic thread, so the
  canvas repaints when a frame arrives or a control changes, and at no other time. Where the
  platform has no worker to spare, the same analysis runs on the logic thread instead.
- **The analyzer is a canvas.** Log-frequency bands, a dB ruler, a scope triggered on a rising
  zero crossing, the step playhead and the peak meter are all `DrawList` calls in
  `rack_canvas.hpp`, drawn from a frame and the settings that made it.
- **No audio device is not a failure.** With no sound card, or when the browser refuses to open
  an audio context, the same render function runs on a timer thread at the block rate, so the
  analyzer and the budget readout stay live and the status bar says why it is silent. A browser
  that only suspends its context is simpler still: SDL keeps calling the callback with a silent
  buffer until the first user gesture resumes it.
- **The synth is pure.** `synth_model.hpp` (polyBLEP oscillator, state-variable filter, envelope,
  delay line, arpeggiator), `spectrum.hpp` (FFT, log bands, scope trigger) and `rt_latest.hpp`
  (the two handoffs) have no threads, no IO and no SDL. `tests/test_synth_rack.cpp` tests them
  directly.

There is no Python counterpart: PRISM's Python bindings have no audio device, and the whole
point of this example is the real-time path.
