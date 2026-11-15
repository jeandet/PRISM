#pragma once

#include "arpeggiator.hpp"
#include "delay.hpp"
#include "envelope.hpp"
#include "filter.hpp"
#include "oscillator.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

// One arpeggiated note at a time, through the whole chain: arpeggiator -> oscillator -> gate
// envelope -> resonant low-pass -> feedback delay -> volume -> soft clip.
namespace prism::synth {

class SynthEngine {
public:
    explicit SynthEngine(double sample_rate = kSampleRate) : sample_rate_(sample_rate) {}

    // What the block played, for the UI to highlight: which step, which note, and whether the
    // gate was open at its end.
    struct Report {
        std::size_t step = 0;
        int note = -1;
        bool sounding = false;
    };

    void reset() noexcept {
        clock_ = {};
        osc_.reset();
        env_.reset();
        filter_.reset();
        delay_.reset();
        tuned_note_ = -2;
    }

    // Renders `out.size()` mono samples from one settings snapshot. No allocation, no locks:
    // this is the code an audio callback runs.
    Report render(const SynthSettings& settings, std::span<float> out) noexcept {
        Report report{};
        if (!settings.powered) {
            std::fill(out.begin(), out.end(), 0.f);
            return report;
        }

        const ArpNotes sequence = arp_sequence(settings.chord, settings.octaves);
        const std::size_t step_frames = step_frames_for(sample_rate_, settings.tempo_bpm);
        const auto gate_frames = static_cast<std::size_t>(static_cast<double>(step_frames) * 0.8);
        env_.set_times(sample_rate_, settings.attack_ms, settings.release_ms);
        filter_.set(sample_rate_, settings.cutoff_hz, settings.resonance);
        delay_.set_time(sample_rate_, settings.delay_ms);
        const auto volume = static_cast<float>(std::pow(10.0, settings.volume_db / 20.0));

        for (float& sample : out) {
            const ArpTick tick = arp_tick(clock_, sequence, settings, step_frames, gate_frames);
            report = {.step = tick.step, .note = tick.note, .sounding = tick.sounding};

            if (tick.note != tuned_note_) {
                tuned_note_ = tick.note;
                osc_.set_increment(tuned_note_ < 0 ? 0.0 : midi_to_hz(tuned_note_) / sample_rate_);
            }
            env_.gate(tick.sounding);
            const float voice = osc_.tick(settings.waveform) * env_.tick();
            const float filtered = filter_.process(voice);
            const float delayed = delay_.process(filtered, settings.delay_feedback, settings.delay_mix);
            sample = soft_clip(delayed * volume);
        }
        return report;
    }

private:
    static float soft_clip(float x) noexcept { return std::tanh(x); }

    double sample_rate_;
    ArpClock clock_{};
    Oscillator osc_{};
    Envelope env_{};
    SvfLowpass filter_{};
    DelayLine delay_{};
    int tuned_note_ = -2;
};

} // namespace prism::synth
