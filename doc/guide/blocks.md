# Blocks

<img src="../logo/block.svg" alt="" width="56" align="right"/>

`Settings` above is a **block**: a plain struct holding its fields (the model), its `view()` and
its reactions (the behaviour). Place one with `vb.block(b)`. Reuse is just declaring more of them:

```cpp
struct ChannelStrip {                       // a mixer channel
    core::Field<ui::Label<>> name;
    core::Field<Knob> gain{{.min = -60, .max = 6, .label = "Gain", .unit = "dB"}};
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};
    void view(app::ViewBuilder& vb) { vb.vstack(name, gain, mute); }
};

struct Mixer {
    std::array<ChannelStrip, 8> strips;     // eight working channels, each with its own state
    void view(app::ViewBuilder& vb) {
        vb.hstack([&] { for (auto& s : strips) vb.block(s); });
    }
};
```

No base class and nothing to register; a block saves and restores like any other field. The
widget gallery's "Build your own" tab runs this mixer, next to the one-header `Knob` widget it uses.

Run any app with `PRISM_SHOW_BLOCKS=1` (or call `tree.show_blocks(true)`) and each block is
outlined and named on screen, as in the [synth rack's](../../examples/model_synth_rack/) second screenshot.

A block that needs the running app (a worker's results, the clock) says so with an `attach()`.
It runs once, on the logic thread, before the app's setup:

```cpp
struct Meter {
    core::Field<ui::Label<>> level;
    core::Shared<double> reading;     // a worker thread sets it
    void view(app::ViewBuilder& vb) { vb.widget(level); }
    void attach(app::AppContext& ctx) {
        ctx.receive(reading, [this](double v) { level.set({fmt::format("{:.1f} dB", v)}); });
    }
};
```

A block keeps its state in fields, and its `view()` shows them. In a debug build the app checks
that every `Field` and `Derived` the model declares was placed by its `view()`, and names the ones
that were not — a forgotten widget, usually. A field a block keeps as a value rather than a widget
(a selection index, the mask behind a row of checkboxes) is marked `[[=core::skip]]`, the same
annotation the automatic UI uses to leave a member out:

```cpp
struct StepGates {
    std::array<core::Field<ui::Checkbox>, 16> boxes{};
    [[=core::skip]] core::Field<std::uint16_t> step_mask{0xFFFF};   // a value, not a widget
};
```

Only a compiler with reflection parses `[[=...]]`, and the check runs only there. Code that must
also build with clang keeps such a field in a small nested struct instead, as the synth rack's step
gates do.
