#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>

#include <exception>
#include <fmt/format.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../showcase/showcase_common.hpp"
#include "audio_thread.hpp"
#include "blocks/analyzer.hpp"
#include "blocks/arpeggiator.hpp"
#include "blocks/effects.hpp"
#include "blocks/output.hpp"
#include "blocks/step_gates.hpp"
#include "blocks/voice.hpp"
#include "rack_settings.hpp"

using namespace prism;

// The app is composition only: six blocks, one settings Derived, one status bar. Everything that
// makes sound lives in AudioThread, everything that makes numbers lives in dsp/, and each panel
// lives in its own file under blocks/.
struct SynthApp {
    RackSettings settings;

    // The display frame and the channel the analysis thread publishes it on. Declared before the
    // blocks: the analyzer and the output panel both read the frame.
    struct Feed {
        core::Field<synth::AnalyzerFrame> frame{};
        core::Shared<synth::AnalyzerFrame> frames{};
    } feed{};

    synth::blocks::StepGates gates{};
    synth::blocks::Arpeggiator arp{};
    synth::blocks::Voice voice{};
    synth::blocks::Effects effects{};
    synth::blocks::Output output{feed.frame};

    // One value crosses to the audio thread: the whole settings snapshot, rebuilt whenever a
    // control it reads moves. In a struct of its own: it is plumbing, not a widget to place.
    struct Link {
        core::Derived<synth::SynthSettings> settings;
    };
    Link link{{[this] { return gather_settings(); }}};

    synth::blocks::Analyzer analyzer{feed.frame, link.settings};

    synth::AudioThread audio{feed.frames};

    core::Derived<std::string> status_note{
        [this] {
            const synth::AnalyzerFrame& shot = feed.frame.get();
            if (!arp.powered()) return std::string("idle");
            return fmt::format("step {:2} · {:>3} · {:5.1f} dBFS", shot.step + 1,
                               synth::note_name(shot.note), shot.peak_db);
        }};
    core::Derived<std::string> status_budget{
        [this] {
            const synth::AnalyzerFrame& shot = feed.frame.get();
            if (shot.callback_max_ms <= 0.f) return std::string("callback -- of -- ms");
            const float budget = shot.sample_rate > 0.0
                                     ? 1000.f * static_cast<float>(shot.buffer_frames)
                                           / static_cast<float>(shot.sample_rate)
                                     : 0.f;
            return fmt::format("callback {:.3f} ms mean of {:.2f} ms", shot.callback_mean_ms,
                               budget);
        }};

    core::Field<ui::Label<>> status_audio{{.value = "engine off — press Power to start"}};

    core::Wiring wire;
    std::jthread analysis{};   // declared after `audio`, so it stops before what it reads goes

    // Set once a window exists. A capture run has neither a window nor an audio thread, so the
    // power button there only changes what is drawn.
    bool attached = false;
    bool offline = false;   // a capture run: rendered once, no threads

    SynthApp() {
        wire.on(
            arp.power, [this](const ui::Button& button) {
                set_power(button.checked.value_or(false));
            });
    }

    // The running app's wiring, on the logic thread before the first frame: settings out to the
    // audio thread, display frames back in, and the analysis worker.
    void attach(app::AppContext& ctx) {
        if (offline) return;
        attached = true;
        link.settings.observe([this](const synth::SynthSettings& live) { audio.publish(live); });
        audio.publish(link.settings.get());
        ctx.receive(feed.frames, [this](const synth::AnalyzerFrame& shot) { feed.frame.set(shot); });
        start_analysis(ctx);
    }

    void start_analysis(app::AppContext& ctx) {
        try {
            analysis = std::jthread([this](std::stop_token stop) { audio.run_analysis(stop); });
        } catch (const std::exception&) {
            // No worker to spare (an exhausted wasm pthread pool) is no reason to leave the
            // window without an analyzer: analyse on the logic thread, once per tick, instead.
            ctx.clock().add([this](auto) {
                audio.analyse_once();
                return true;
            });
        }
    }

    synth::SynthSettings gather_settings() const {
        return synth::make_settings(arp.settings(gates.mask()), voice.settings(), effects.settings(),
                                    output.volume_db());
    }

    void set_power(bool on) {
        if (!attached) return;
        if (on == audio.is_open()) return;
        if (!on) {
            audio.close();
            status_audio.set(ui::Label<>{"engine off — press Power to start"});
            return;
        }
        audio.open();
        status_audio.set(ui::Label<>{audio.status_text()});
    }

    // Headless capture: run the same audio path offline, then show the analyzer holding a
    // realistic frame. No device, no threads.
    void seed_demo_data() {
        offline = true;
        arp.power.set({.text = "Power", .primary = true, .checked = true});
        status_audio.set(ui::Label<>{"capture run — rendered offline, no audio device"});
        audio.publish(gather_settings());
        feed.frame.set(audio.render_offline(8));
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(settings.layout,
                    {
                        {"analyzer", "Analyzer",
                         [&](app::WidgetTree::ViewBuilder& p) { p.block(analyzer); }},
                        {"steps", "Step Gates — 16th notes",
                         [&](app::WidgetTree::ViewBuilder& p) { p.block(gates); }},
                        {"arp", "Arpeggiator",
                         [&](app::WidgetTree::ViewBuilder& p) { p.scroll([&] { p.block(arp); }); }},
                        {"voice", "Voice",
                         [&](app::WidgetTree::ViewBuilder& p) {
                             p.scroll([&] {
                                 p.block(voice);
                                 p.block(effects);
                             });
                         }},
                        {"engine", "Output & Callback Budget",
                         [&](app::WidgetTree::ViewBuilder& p) { p.scroll([&] { p.block(output); }); }},
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
    auto app = std::make_unique<SynthApp>();
    if (argc >= 2) {
        app->seed_demo_data();
        return showcase(argc, argv, *app, 1200, 780);
    }
    return app::run(*app, {.title = "PRISM Synth Rack", .width = 1200, .height = 780,
                           .settings = &app->settings, .organization = "prism", .name = "synth-rack"});
}
