#pragma once

#include <prism/prism.hpp>

#include <chrono>
#include <fmt/format.h>
#include <string>
#include <thread>
#include <vector>

#include "../../widget_gallery/knob.hpp"
#include "../dsp/spectrum.hpp"

namespace prism::synth::blocks {

// The output panel: master volume, the callback budget readout, and the stall button that shows
// the audio thread never waits for the one that owns the model. The display frame is read through
// the captions' Derived sources, never through a widget.
struct Output {
    explicit Output(core::Field<AnalyzerFrame>& shot) : frame{shot} {
        reactions_.push_back(stall_button.on_change().connect([this](const ui::Button&) {
            block_this_thread(kStall);
            note.set(ui::Label<>{"the logic thread was blocked for 2 s; the audio callback kept "
                                 "rendering, and the analyzer kept filling in"});
        }));
    }

    core::Field<AnalyzerFrame>& frame;   // the app's: read by the captions below
    core::Field<gallery::Knob> volume{
        {.value = -8.0, .min = -40.0, .max = 0.0, .step = 0.5, .label = "Volume", .unit = "dB"}};
    core::Field<ui::Button> stall_button{{.text = "Stall the logic thread (2 s)", .primary = false}};
    core::Field<ui::Label<>> note{
        {.value = "the audio thread never waits for the UI: start the engine and press the button",
         .wrap = true}};

    core::Derived<std::string> budget_caption{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            if (shot.callback_max_ms <= 0.f) return std::string("callback: no audio yet");
            return fmt::format("callback {:.3f} ms mean · {:.3f} ms max", shot.callback_mean_ms,
                               shot.callback_max_ms);
        }};
    core::Derived<std::string> engine_caption{
        [this] {
            const AnalyzerFrame& shot = frame.get();
            const std::string updates = shot.parameter_updates == 1
                                            ? std::string("1 update")
                                            : fmt::format("{} updates", shot.parameter_updates);
            return fmt::format("of {:.2f} ms · {} Hz · {} frames · {}", block_ms(shot),
                               static_cast<int>(shot.sample_rate), shot.buffer_frames, updates);
        }};

    double volume_db() const { return volume.get().value; }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(volume);
            vb.spacer();
        });
        vb.widget(budget_caption);
        vb.widget(engine_caption);
        vb.widget(stall_button).tooltip("Block the thread that owns the model for 2 s");
        vb.widget(note);
    }

private:
    static constexpr std::chrono::seconds kStall{2};

    static void block_this_thread(std::chrono::seconds total) {
        const auto deadline = std::chrono::steady_clock::now() + total;
        while (std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // How long one block of that audio takes to play: the deadline the callback works against.
    static float block_ms(const AnalyzerFrame& shot) {
        return shot.sample_rate > 0.0 ? 1000.f * static_cast<float>(shot.buffer_frames)
                                            / static_cast<float>(shot.sample_rate)
                                      : 0.f;
    }

    // Declared last, so the connection goes before the field it observes.
    std::vector<core::Connection> reactions_{};
};

} // namespace prism::synth::blocks
