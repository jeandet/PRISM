#pragma once

#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

// The arpeggiator, as plain functions and one clock: which chord, which note each step plays,
// and when the step advances. No state beyond the clock, so it is all directly testable.
namespace prism::synth {

// Steps in the pattern. Also the number of gate bits in ArpSettings::step_mask.
inline constexpr std::size_t kSteps = 16;

inline constexpr int kNotesPerChord = 4;
inline constexpr std::size_t kMaxArpNotes = 12;   // four chord tones over three octaves
inline constexpr int kMiddleC = 60;

// A chord is a root and the intervals above it, so the octave span is just +12 per octave.
struct ChordShape {
    int root = kMiddleC;
    std::array<int, kNotesPerChord> intervals{0, 4, 7, 11};
};

constexpr ChordShape chord_shape(Chord chord) {
    switch (chord) {
        case Chord::Cmaj7:  return {60, {0, 4, 7, 11}};   // C  E  G  B
        case Chord::Am9:    return {57, {0, 3, 7, 10}};   // A  C  E  G
        case Chord::Dm7:    return {62, {0, 3, 7, 10}};   // D  F  A  C
        case Chord::G7sus4: return {55, {0, 5, 7, 10}};   // G  C  D  F
        case Chord::Fmaj7:  return {53, {0, 4, 7, 11}};   // F  A  C  E
        case Chord::Em7:    return {52, {0, 3, 7, 10}};   // E  G  B  D
    }
    return {};
}

inline double midi_to_hz(int note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

inline std::string note_name(int note) {
    static constexpr std::array<const char*, 12> names{"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};
    if (note < 0) return "--";
    return std::string(names[static_cast<std::size_t>(note % 12)]) + std::to_string(note / 12 - 1);
}

// The notes the arpeggiator walks, low to high, one chord tone per octave.
struct ArpNotes {
    std::array<int, kMaxArpNotes> notes{};
    std::size_t count = 0;

    int operator[](std::size_t i) const { return notes[i]; }
};

constexpr ArpNotes arp_sequence(Chord chord, int octaves) {
    ArpNotes out{};
    const ChordShape shape = chord_shape(chord);
    for (int octave = 0; octave < std::clamp(octaves, 1, 3); ++octave)
        for (int interval : shape.intervals) {
            if (out.count == kMaxArpNotes) return out;
            out.notes[out.count++] = shape.root + interval + 12 * octave;
        }
    return out;
}

constexpr bool step_enabled(std::uint16_t mask, std::size_t step) {
    return ((mask >> (step % kSteps)) & 1u) != 0u;
}

// Deterministic per-step randomness: the same step always picks the same note, so Random is a
// pure function of the step index -- stable across blocks, and testable.
constexpr std::uint32_t hash_step(std::size_t step) {
    std::uint32_t x = static_cast<std::uint32_t>(step) + 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x21F0AAADu;
    x = (x ^ (x >> 15)) * 0x735A2D97u;
    return x ^ (x >> 15);
}

// Which of `sequence`'s notes step `step` plays.
constexpr std::size_t arp_index(std::size_t count, ArpPattern pattern, std::size_t step) {
    if (count == 0) return 0;
    switch (pattern) {
        case ArpPattern::Up:   return step % count;
        case ArpPattern::Down: return count - 1 - (step % count);
        case ArpPattern::UpDown: {
            if (count == 1) return 0;
            const std::size_t span = count * 2 - 2;   // up, then down, ends not repeated
            const std::size_t i = step % span;
            return i < count ? i : span - i;
        }
        case ArpPattern::Random: return hash_step(step) % count;
    }
    return 0;
}

constexpr int arp_note_for(const ArpNotes& sequence, ArpPattern pattern, std::size_t step) {
    return sequence.count == 0 ? -1 : sequence[arp_index(sequence.count, pattern, step)];
}

// Where the arpeggiator is inside its bar of kSteps.
struct ArpClock {
    std::size_t step = 0;
    std::size_t frame = 0;   // frames since this step started
};

struct ArpTick {
    std::size_t step = 0;
    int note = -1;          // the note this step plays, whether or not the gate is open
    bool sounding = false;  // the step is enabled and still inside its gate
};

// Anything carrying the arpeggiator's part: its own ArpSettings, or the whole snapshot the
// renderer holds -- the tick reads only these two fields, so it takes either.
template <typename Settings>
concept ArpSettingsLike = requires(const Settings& s) {
    s.pattern;
    s.step_mask;
};

// One sample of the arpeggiator. `step_frames` is a 16th note at the tempo; the gate holds for
// `gate_frames` of it and the rest is the envelope's release.
template <ArpSettingsLike Settings>
inline ArpTick arp_tick(ArpClock& clock, const ArpNotes& sequence, const Settings& settings,
                        std::size_t step_frames, std::size_t gate_frames) {
    const ArpTick tick{.step = clock.step,
                       .note = arp_note_for(sequence, settings.pattern, clock.step),
                       .sounding = clock.frame < gate_frames
                                   && step_enabled(settings.step_mask, clock.step)};
    if (++clock.frame >= step_frames) {
        clock.frame = 0;
        clock.step = (clock.step + 1) % kSteps;
    }
    return tick;
}

// One 16th note at `bpm`, in frames.
inline std::size_t step_frames_for(double sample_rate, double bpm) {
    const double frames = sample_rate * 60.0 / std::max(bpm, 1.0) / 4.0;
    return static_cast<std::size_t>(std::max(1.0, std::round(frames)));
}

} // namespace prism::synth
