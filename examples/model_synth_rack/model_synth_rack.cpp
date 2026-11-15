#include <prism/prism.hpp>
#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>
#include <prism/ui/dock.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fmt/format.h>
#include <span>
#include <string>
#include <memory>
#include <thread>
#include <vector>

#include "../showcase/showcase_common.hpp"
#include "audio_device.hpp"
#include "rack_canvas.hpp"
#include "rt_latest.hpp"
#include "spectrum.hpp"
#include "synth_model.hpp"

using namespace prism;
using namespace prism::synth;

namespace {

using namespace std::chrono_literals;
using steady = std::chrono::steady_clock;

constexpr int kDeviceRate = static_cast<int>(kSampleRate);
constexpr int kBufferFrames = 512;
constexpr int kChannels = 2;
constexpr auto kAnalysisPeriod = 33ms;   // ~30 frames per second
constexpr auto kSleepSlice = 5ms;
constexpr auto kStall = 2s;

// Sleeps in slices so a stop request is noticed while waiting; true when one arrived.
bool sleep_slices(const std::stop_token& stop, std::chrono::milliseconds total) {
    for (auto slept = 0ms; slept < total; slept += kSleepSlice) {
        if (stop.stop_requested()) return true;
        std::this_thread::sleep_for(kSleepSlice);
    }
    return stop.stop_requested();
}

// Blocks the calling thread on purpose: this is what the Output panel's stall button
// demonstrates. Nothing on the audio path waits for it.
void stall_for(std::chrono::milliseconds total) {
    const auto deadline = steady::now() + total;
    while (steady::now() < deadline) std::this_thread::sleep_for(20ms);
}

float block_budget_ms(const AnalyzerFrame& shot) {
    return shot.sample_rate > 0.0
               ? 1000.f * static_cast<float>(shot.buffer_frames) / static_cast<float>(shot.sample_rate)
               : 0.f;
}

} // namespace

// Docked panel layout, persisted like any other setting. Outside the anonymous namespace:
// the settings file is reflected over a type with external linkage.
struct SynthPrefs {
    core::Field<ui::DockLayout> layout{{ui::dock_split(
        /*vertical=*/true,
        {ui::dock_split(/*vertical=*/false,
                        {ui::dock_group({"analyzer"}),
                         ui::dock_split(/*vertical=*/true,
                                        {ui::dock_group({"arp"}), ui::dock_group({"voice"}),
                                         ui::dock_group({"engine"})},
                                        {0.31f, 0.41f, 0.28f})},
                        {0.68f, 0.32f}),
         ui::dock_group({"steps"})},
        {0.85f, 0.15f})}};
    app::WindowGeometry window;
};

struct SynthApp {
    SynthPrefs prefs;

    // One Field per control. The settings value the audio thread reads is a Derived of them.
    core::Field<ui::Button> power{{.text = "Power", .primary = true, .checked = false}};
    core::Field<ui::Slider<double>> tempo{
        {.value = 96.0, .min = 60.0, .max = 200.0, .step = 1.0, .label = "Tempo", .unit = "BPM"}};
    core::Field<Chord> chord{Chord::Cmaj7};
    core::Field<ArpPattern> pattern{ArpPattern::Up};
    core::Field<ui::Slider<double>> octaves{
        {.value = 2.0, .min = 1.0, .max = 3.0, .step = 1.0, .label = "Octave span", .unit = "oct"}};

    core::Field<Waveform> waveform{Waveform::Saw};
    core::Field<ui::Slider<double>> cutoff{
        {.value = 2400.0, .min = 100.0, .max = 12000.0, .step = 10.0, .label = "Cutoff", .unit = "Hz"}};
    core::Field<ui::Slider<double>> resonance{
        {.value = 1.4, .min = 0.5, .max = 12.0, .step = 0.1, .label = "Resonance", .unit = "Q"}};
    core::Field<ui::Slider<double>> attack{
        {.value = 6.0, .min = 1.0, .max = 400.0, .step = 1.0, .label = "Attack", .unit = "ms"}};
    core::Field<ui::Slider<double>> release{
        {.value = 180.0, .min = 20.0, .max = 1200.0, .step = 5.0, .label = "Release", .unit = "ms"}};
    core::Field<ui::Slider<double>> delay_time{
        {.value = 260.0, .min = 20.0, .max = 600.0, .step = 5.0, .label = "Delay time", .unit = "ms"}};
    core::Field<ui::Slider<double>> delay_feedback{
        {.value = 0.32, .min = 0.0, .max = 0.85, .step = 0.01, .label = "Delay feedback"}};
    core::Field<ui::Slider<double>> delay_mix{
        {.value = 0.22, .min = 0.0, .max = 1.0, .step = 0.01, .label = "Delay mix"}};
    core::Field<ui::Slider<double>> volume{
        {.value = -8.0, .min = -40.0, .max = 0.0, .step = 0.5, .label = "Master volume", .unit = "dB"}};

    core::Field<ui::Button> stall_button{{.text = "Stall the logic thread (2 s)", .primary = false}};

    // A checkbox shows its state with a tick in the accent colour; a pressed secondary button
    // is only a shade darker and reads as identical at a glance.
    std::array<core::Field<ui::Checkbox>, kSteps> steps{};

    // Nested so the tree's unplaced-field check, which only looks at this model's own members,
    // has nothing to say about a mask that is not a widget.
    struct Gates {
        core::Field<std::uint16_t> mask{0xFFFF};
    } gates{};

    // Everything that crosses a thread. The logic thread publishes settings and drains frames;
    // the audio thread reads settings and writes samples; the analyzer reads samples and
    // publishes frames.
    struct AudioLink {
        core::Derived<SynthSettings> settings;
        RtLatest<SynthSettings> to_audio{};
        SampleRing samples{1u << 14};
        struct Telemetry {
            std::atomic<std::uint32_t> parameter_updates{0};
            std::atomic<std::uint32_t> step{0};
            std::atomic<int> note{-1};
            std::atomic<bool> sounding{false};
        } telemetry{};
        AudioDevice device{{kDeviceRate, kChannels, kBufferFrames}};
        core::Shared<AnalyzerFrame> frames{};
    } link;

    core::Field<AnalyzerFrame> frame{};

    core::Derived<std::string> arp_caption{
        [this] {
            const int octave_span = static_cast<int>(std::lround(octaves.get().value));
            return fmt::format("{} · {} · {:.0f} BPM · {} oct · {} notes",
                               ui::enum_label<Chord>(ui::enum_index(chord.get())),
                               ui::enum_label<ArpPattern>(ui::enum_index(pattern.get())),
                               tempo.get().value, octave_span,
                               arp_sequence(chord.get(), octave_span).count);
        },
        tempo, chord, pattern, octaves};
    core::Derived<std::string> voice_caption{
        [this] {
            return fmt::format("{} · cutoff {:.0f} Hz · Q {:.1f} · A {:.0f} / R {:.0f} ms",
                               ui::enum_label<Waveform>(ui::enum_index(waveform.get())),
                               cutoff.get().value, resonance.get().value, attack.get().value,
                               release.get().value);
        },
        waveform, cutoff, resonance, attack, release};
    core::Derived<std::string> budget_caption{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            if (shot.callback_max_ms <= 0.f) return std::string("callback: no audio yet");
            return fmt::format("callback {:.3f} ms mean · {:.3f} ms max", shot.callback_mean_ms,
                               shot.callback_max_ms);
        },
        frame};
    core::Derived<std::string> engine_caption{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            const std::string updates = shot.parameter_updates == 1
                                            ? std::string("1 update")
                                            : fmt::format("{} updates", shot.parameter_updates);
            return fmt::format("of {:.2f} ms · {} Hz · {} frames · {}", block_budget_ms(shot),
                               static_cast<int>(shot.sample_rate), shot.buffer_frames, updates);
        },
        frame};
    core::Derived<std::string> status_note{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            if (!power.get().checked.value_or(false)) return std::string("idle");
            return fmt::format("step {:2} · {:>3} · {:5.1f} dBFS", shot.step + 1, note_name(shot.note),
                               shot.peak_db);
        },
        frame, power};
    core::Derived<std::string> status_budget{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            if (shot.callback_max_ms <= 0.f) return std::string("callback -- of -- ms");
            return fmt::format("callback {:.3f} ms mean of {:.2f} ms", shot.callback_mean_ms,
                               block_budget_ms(shot));
        },
        frame};

    core::Field<ui::Label<>> status_audio{{.value = "engine off — press Power to start"}};
    core::Field<ui::Label<>> stall_note{
        {.value = "the audio thread never waits for the UI: start the engine and press the button",
         .wrap = true}};

    std::vector<core::Connection> wiring{};

    // Set once a window exists. A capture run has neither a window nor an audio thread, so the
    // power button there only changes what is drawn.
    bool attached = false;

    SynthEngine engine{kSampleRate};
    SpectrumAnalyzer spectrum{kSampleRate};
    std::array<float, AudioDevice::kMaxFrames> mono_scratch{};
    std::vector<core::Point> trace_scratch{};

    // Audio-thread state, owned by the render callback.
    SynthSettings last_settings{};

    // Analysis state, owned by whichever thread analyses: the analyzer thread, or the logic
    // thread when the platform has no worker to spare.
    AnalyzerFrame last_shot{};

    SynthApp()
        : link{.settings = {[this] { return gather_settings(); }, power, tempo, chord, pattern, octaves,
                            gates.mask, waveform, cutoff, resonance, attack, release, delay_time,
                            delay_feedback, delay_mix, volume}} {
        for (std::size_t step = 0; step < kSteps; ++step) {
            steps[step].set({.checked = true, .label = fmt::format("{}", step + 1)});
            wiring.push_back(steps[step].on_change().connect([this, step](const ui::Checkbox& box) {
                set_step_gate(step, box.checked);
            }));
        }
        wiring.push_back(power.on_change().connect(
            [this](const ui::Button& button) { set_power(button.checked.value_or(false)); }));
        wiring.push_back(stall_button.on_change().connect([this](const ui::Button&) {
            stall_for(kStall);
            stall_note.set(ui::Label<>{"the logic thread was blocked for 2 s; the audio callback "
                                       "kept rendering, and the analyzer kept filling in"});
        }));
    }

    void attach() { attached = true; }

    void drain() { link.frames.drain_notifications(); }

    SynthSettings gather_settings() const {
        return {.powered = power.get().checked.value_or(false),
                .tempo_bpm = tempo.get().value,
                .chord = chord.get(),
                .pattern = pattern.get(),
                .octaves = static_cast<int>(std::lround(octaves.get().value)),
                .step_mask = gates.mask.get(),
                .waveform = waveform.get(),
                .cutoff_hz = cutoff.get().value,
                .resonance = resonance.get().value,
                .attack_ms = attack.get().value,
                .release_ms = release.get().value,
                .delay_ms = delay_time.get().value,
                .delay_feedback = delay_feedback.get().value,
                .delay_mix = delay_mix.get().value,
                .volume_db = volume.get().value};
    }

    void set_step_gate(std::size_t step, bool open) {
        const auto bit = static_cast<std::uint16_t>(1u << step);
        const std::uint16_t mask = gates.mask.get();
        gates.mask.set(open ? static_cast<std::uint16_t>(mask | bit)
                            : static_cast<std::uint16_t>(mask & static_cast<std::uint16_t>(~bit)));
    }

    void set_power(bool on) {
        if (!attached) return;
        if (on == link.device.is_open()) return;
        if (!on) {
            link.device.close();
            status_audio.set(ui::Label<>{"engine off — press Power to start"});
            return;
        }
        engine.reset();   // no audio thread exists yet: the device is closed
        link.device.open(
            [this](float* interleaved, int frames) { render_audio(interleaved, frames); });
        status_audio.set(ui::Label<>{audio_status_text()});
    }

    std::string audio_status_text() const {
        const auto config = link.device.config();
        if (link.device.is_playing())
            return fmt::format("audio {} Hz · {} frame block · {:.2f} ms per block", config.sample_rate,
                               config.buffer_frames, link.device.budget_ms());
        return fmt::format("no audio device ({}) — still rendering, silently", link.device.error());
    }

    // The audio thread: one settings snapshot in, one block of interleaved samples out. Never
    // blocks, never allocates, never touches a Field.
    void render_audio(float* interleaved, int frames) {
        const SynthSettings settings = link.to_audio.read();
        if (!(settings == last_settings)) {
            last_settings = settings;
            link.telemetry.parameter_updates.fetch_add(1, std::memory_order_relaxed);
        }

        const auto count = static_cast<std::size_t>(frames);
        const std::span<float> mono{mono_scratch.data(), count};
        const SynthEngine::Report report = engine.render(settings, mono);

        for (std::size_t frame = 0; frame < count; ++frame) {
            interleaved[2 * frame] = mono[frame];
            interleaved[2 * frame + 1] = mono[frame];
        }
        link.samples.write(mono);
        link.telemetry.step.store(static_cast<std::uint32_t>(report.step), std::memory_order_relaxed);
        link.telemetry.note.store(report.note, std::memory_order_relaxed);
        link.telemetry.sounding.store(report.sounding, std::memory_order_relaxed);
    }

    AnalyzerFrame make_frame(std::span<const float> newest) {
        AnalyzerFrame shot = spectrum.analyze(newest);
        const CallbackLoad load = link.device.load();
        shot.callback_max_ms = load.max_ms;
        shot.callback_mean_ms = load.mean_ms;
        shot.buffer_frames = static_cast<std::uint32_t>(link.device.config().buffer_frames);
        shot.sample_rate = link.device.config().sample_rate;
        shot.parameter_updates = link.telemetry.parameter_updates.load(std::memory_order_relaxed);
        shot.step = link.telemetry.step.load(std::memory_order_relaxed);
        shot.note = link.telemetry.note.load(std::memory_order_relaxed);
        shot.sounding = link.telemetry.sounding.load(std::memory_order_relaxed);
        return shot;
    }

    // One display frame from the newest samples, published only when it differs from the last
    // one, so an idle synth does not wake the window thirty times a second.
    void analyse_once() {
        std::array<float, kFftSize> window{};
        const std::size_t got = link.samples.read_newest(window);
        const AnalyzerFrame shot = make_frame(std::span<const float>(window.data(), got));
        if (shot == last_shot) return;
        last_shot = shot;
        link.frames.set(shot);
    }

    // The analysis thread: newest samples in, one display frame out, ~30 times per second.
    void run_analysis(const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            analyse_once();
            if (sleep_slices(stop, kAnalysisPeriod)) return;
        }
    }

    // Headless capture: run the same audio path offline, then show the analyzer holding a
    // realistic frame. No device, no threads.
    void seed_demo_data() {
        power.set({.text = "Power", .primary = true, .checked = true});
        status_audio.set(ui::Label<>{"capture run — rendered offline, no audio device"});
        link.to_audio.publish(gather_settings());

        std::array<float, kChannels * AudioDevice::kMaxFrames> interleaved{};
        float total_ms = 0.f;
        float slowest_ms = 0.f;
        for (int block = 0; block < 8; ++block) {
            const auto began = steady::now();
            render_audio(interleaved.data(), AudioDevice::kMaxFrames);
            const float elapsed =
                std::chrono::duration<float, std::milli>(steady::now() - began).count();
            total_ms += elapsed;
            slowest_ms = std::max(slowest_ms, elapsed);
        }

        std::array<float, kFftSize> window{};
        const std::size_t got = link.samples.read_newest(window);
        AnalyzerFrame shot = make_frame(std::span<const float>(window.data(), got));
        shot.callback_mean_ms = total_ms / 8.f;   // measured here: a capture run has no callback
        shot.callback_max_ms = slowest_ms;
        frame.set(shot);
    }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        rack_canvas::draw(dl, bounds, node, frame.get(), gather_settings(), trace_scratch);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(prefs.layout,
                    {
                        {"analyzer", "Analyzer",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.canvas(*this).depends_on(frame, link.settings);
                         }},
                        {"steps", "Step Gates — 16th notes",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.hstack([&] {
                                 for (std::size_t step = 0; step < kSteps; ++step)
                                     p.widget(steps[step]).tooltip(
                                         fmt::format("Gate for step {}", step + 1));
                             });
                         }},
                        {"arp", "Arpeggiator",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.scroll([&] {
                                 p.widget(power).tooltip("Open the audio device and start the engine");
                                 p.widget(tempo);
                                 p.widget(chord);
                                 p.widget(pattern);
                                 p.widget(octaves);
                                 p.widget(arp_caption);
                             });
                         }},
                        {"voice", "Voice",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.scroll([&] {
                                 p.widget(waveform);
                                 p.widget(cutoff);
                                 p.widget(resonance);
                                 p.widget(attack);
                                 p.widget(release);
                                 p.widget(delay_time);
                                 p.widget(delay_feedback);
                                 p.widget(delay_mix);
                                 p.widget(voice_caption);
                             });
                         }},
                        {"engine", "Output & Callback Budget",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.scroll([&] {
                                 p.widget(volume);
                                 p.widget(budget_caption);
                                 p.widget(engine_caption);
                                 p.widget(stall_button)
                                     .tooltip("Block the thread that owns the model for 2 s");
                                 p.widget(stall_note);
                             });
                         }},
                    });
            vb.status_bar([&] {
                vb.widget(status_audio);
                vb.spacer();
                vb.widget(status_note);
                vb.gap(18.f);
                vb.widget(status_budget);
            });
        });
    }
};

int main(int argc, char* argv[]) {
    // On the heap: the delay line and the FFT buffers are ~200 KB, and a wasm stack is 64 KB.
    auto owner = std::make_unique<SynthApp>();
    SynthApp& app = *owner;
    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1200, 780);
    }

    std::jthread analysis;
    app::SettingsFile settings{app.prefs,
                               backends::settings_dir("prism", "synth-rack") / "settings.toml"};

    app::model_app(
        app.prefs.window.applied_to({.title = "PRISM Synth Rack",
                                     .width = 1200,
                                     .height = 780,
                                     .decoration = app::DecorationMode::Custom}),
        app, [&](app::AppContext& ctx) {
            ctx.remember_window_size(app.prefs.window);
            app.attach();
            app.link.settings.observe(
                [&app](const SynthSettings& live) { app.link.to_audio.publish(live); });
            app.link.to_audio.publish(app.link.settings.get());
            app.link.frames.set_waker(ctx.waker());
            app.link.frames.observe([&app](const AnalyzerFrame& shot) { app.frame.set(shot); });
            try {
                analysis = std::jthread([&app](std::stop_token stop) { app.run_analysis(stop); });
            } catch (const std::exception&) {
                // No worker to spare — an exhausted wasm pthread pool, for instance — is not a
                // reason to leave the window without an analyzer: analyse on the logic thread,
                // once per animation tick, instead.
                ctx.clock().add([&app](auto) {
                    app.analyse_once();
                    return true;
                });
            }
        });
    return 0;
}
