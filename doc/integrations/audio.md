# Audio

`audio::Output` plays sound. You give it a function that fills a block of samples. It calls that
function on a real-time thread of its own, as fast as the sound card plays.

## A tone you can tune

```cpp
#include <prism/prism.hpp>
#include <prism/audio/output.hpp>
#include <cmath>
#include <memory>
#include <numbers>
using namespace prism;

struct Tone {
    core::Field<ui::Slider<>> pitch{{.value = 440, .min = 110, .max = 880, .step = 1,
                                     .label = "Pitch", .unit = "Hz"}};
    core::Field<ui::Checkbox> play{{.checked = false, .label = "Play"}};

    core::Latest<float> to_audio{440.f};    // the pitch, handed to the audio thread
    std::unique_ptr<audio::Output> output;  // sound while it exists
    float phase = 0.f;                      // the audio thread's own

    void attach(app::AppContext&) {
        pitch.observe([this](const ui::Slider<>& s) { to_audio.publish(float(s.value)); });
        play.observe([this](const ui::Checkbox& c) {
            if (c.checked)
                output = std::make_unique<audio::Output>(audio::Format{},
                                                         [this](std::span<float> out) { render(out); });
            else
                output.reset();   // stops the thread; render is never called again
        });
    }

    // On the audio thread: no lock, no allocation, no Field.
    void render(std::span<float> out) {
        const float hz = to_audio.read();
        const float step = 2.f * std::numbers::pi_v<float> * hz / 48000.f;
        for (std::size_t i = 0; i + 1 < out.size(); i += 2) {   // two channels, interleaved
            out[i] = out[i + 1] = 0.2f * std::sin(phase);
            phase = std::fmod(phase + step, 2.f * std::numbers::pi_v<float>);
        }
    }

    void view(app::ViewBuilder& vb) { vb.vstack(pitch, play); }
};

int main() {
    Tone tone;
    return app::run(tone, {.title = "Tone", .width = 320, .height = 120});
}
```

Three threads meet here, and each keeps to its own data:

1. The logic thread owns the fields. When the slider moves, it publishes the new pitch into a
   `core::Latest<float>`.
2. The audio thread reads the latest pitch at the start of each block and fills the samples.
   It never waits for the logic thread.
3. The render thread draws the window, as always.

## Why Latest, and not Shared

The render function runs on a real-time thread. If it waits, even briefly, you hear a click.

- A `core::Shared<T>` allocates when it is set, and a `core::Channel<T>` allocates per message.
  Neither may run there.
- A `core::Latest<T>` never locks and never allocates. The writer copies the value in; the reader
  gets the newest complete value. It takes one writer and one reader, and `T` must be trivially
  copyable: numbers, or a plain struct of them.

To send results the other way (levels, a spectrum to draw), let the audio thread write into a
buffer of its own and have another thread, or a timer, publish it through a `core::Shared`. The
synth rack example does this for its spectrum analyzer.

## When there is no sound card

If no device opens (no sound card, a headless machine, a browser page that has not started its
audio yet), a timer thread calls the same render function at the same rate. Exactly one thread
renders either way, so whatever reads the samples stays live. `playing()` says which one runs.

In the browser, audio may only start after the user clicked or pressed a key. Open the `Output`
from a button or a checkbox, as above.

## Reference

`audio::Format`:

| Member | Default | Meaning |
|---|---|---|
| `sample_rate` | 48000 | Samples per second, per channel. |
| `channels` | 2 | 1 for mono, 2 for stereo. |
| `block_frames` | 512 | Frames per call when the timer renders. A device may ask for other sizes. |

The render function, `audio::RenderFn`, is `void(std::span<float> interleaved)`. The span holds
`frames * channels` samples, channels interleaved (left, right, left, right...). `frames` is at
most `audio::kMaxBlockFrames` (2048). Samples go from -1 to 1.

`audio::Output`:

| Call | What it does |
|---|---|
| `Output(Format, RenderFn)` | Starts rendering at once, on a device, or on the timer if none opens. |
| `~Output()` | Stops the thread. The render function is never called after the destructor returns. |
| `playing()` | True when a device plays the sound. False when the timer renders, silently. |
| `error()` | Why no device opened. |
| `format()` | The format in use. |
| `load()` | What the render function costs over the last second of audio (below). |

`audio::Load`:

| Member | Meaning |
|---|---|
| `max_ms`, `mean_ms` | The longest and the mean time one call took. |
| `calls` | How many calls. |
| `budget_ms` | How long one block takes to play. A call longer than this is heard as a click. |

## Pitfalls

- Inside the render function: no lock, no allocation, no file, no `Field`. Read what you need
  from a `core::Latest`.
- The render function may outlive the code that made it only until the `Output` is destroyed.
  Destroy the `Output` before what the function captures.
- There is no Python binding for audio.
