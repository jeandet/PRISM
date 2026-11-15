#pragma once

#include <cstdint>

// What the panels hand the audio thread. Each block owns its own small struct; `make_settings`
// pastes the four of them side by side into the single snapshot the renderer copies per block.
namespace prism::synth {

// Every time constant is parameterized by this one, so the whole synth can move to another rate.
inline constexpr double kSampleRate = 48000.0;

enum class Waveform { Sine, Saw, Square, Triangle };
enum class Chord { Cmaj7, Am9, Dm7, G7sus4, Fmaj7, Em7 };
enum class ArpPattern { Up, Down, UpDown, Random };

// The arpeggiator panel's part.
struct ArpSettings {
    bool powered = false;
    double tempo_bpm = 96.0;
    Chord chord = Chord::Cmaj7;
    ArpPattern pattern = ArpPattern::Up;
    int octaves = 2;
    std::uint16_t step_mask = 0xFFFF;   // bit n set: step n plays
};

// The voice panel's part.
struct VoiceSettings {
    Waveform waveform = Waveform::Saw;
    double cutoff_hz = 2400.0;
    double resonance = 1.4;             // filter Q
    double attack_ms = 6.0;
    double release_ms = 180.0;
};

// The effects panel's part: the feedback delay.
struct EffectsSettings {
    double delay_ms = 260.0;
    double delay_feedback = 0.32;
    double delay_mix = 0.22;
};

// Everything the audio thread needs, in one trivially copyable value. The renderer takes a
// whole snapshot per block, so a control moving mid-block cannot leave it half-updated.
struct SynthSettings {
    bool powered = false;

    // Arpeggiator
    double tempo_bpm = 96.0;
    Chord chord = Chord::Cmaj7;
    ArpPattern pattern = ArpPattern::Up;
    int octaves = 2;
    std::uint16_t step_mask = 0xFFFF;

    // Voice
    Waveform waveform = Waveform::Saw;
    double cutoff_hz = 2400.0;
    double resonance = 1.4;
    double attack_ms = 6.0;
    double release_ms = 180.0;

    // Effects and output
    double delay_ms = 260.0;
    double delay_feedback = 0.32;
    double delay_mix = 0.22;
    double volume_db = -8.0;

    bool operator==(const SynthSettings&) const = default;
};

inline SynthSettings make_settings(const ArpSettings& arp, const VoiceSettings& voice,
                                  const EffectsSettings& effects, double volume_db) {
    return {.powered = arp.powered,
            .tempo_bpm = arp.tempo_bpm,
            .chord = arp.chord,
            .pattern = arp.pattern,
            .octaves = arp.octaves,
            .step_mask = arp.step_mask,
            .waveform = voice.waveform,
            .cutoff_hz = voice.cutoff_hz,
            .resonance = voice.resonance,
            .attack_ms = voice.attack_ms,
            .release_ms = voice.release_ms,
            .delay_ms = effects.delay_ms,
            .delay_feedback = effects.delay_feedback,
            .delay_mix = effects.delay_mix,
            .volume_db = volume_db};
}

} // namespace prism::synth
