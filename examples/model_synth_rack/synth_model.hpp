#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>

// The synth, as plain functions and small classes: no threads, no IO, no SDL, every time
// constant parameterized by the sample rate. `render()` is the real-time path and allocates
// nothing once the engine exists; tests/test_synth_rack.cpp drives all of it directly.
namespace prism::synth {

inline constexpr double kSampleRate = 48000.0;

// Steps in the pattern. Also the number of gate bits in SynthSettings::step_mask.
inline constexpr std::size_t kSteps = 16;

enum class Waveform { Sine, Saw, Square, Triangle };
enum class Chord { Cmaj7, Am9, Dm7, G7sus4, Fmaj7, Em7 };
enum class ArpPattern { Up, Down, UpDown, Random };

// Everything the audio thread needs, in one trivially copyable value. The renderer takes a
// whole snapshot per block, so a control moving mid-block cannot leave it half-updated.
struct SynthSettings {
    bool powered = false;

    // Arpeggiator
    double tempo_bpm = 96.0;
    Chord chord = Chord::Cmaj7;
    ArpPattern pattern = ArpPattern::Up;
    int octaves = 2;
    std::uint16_t step_mask = 0xFFFF;   // bit n set: step n plays

    // Voice
    Waveform waveform = Waveform::Saw;
    double cutoff_hz = 2400.0;
    double resonance = 1.4;             // filter Q
    double attack_ms = 6.0;
    double release_ms = 180.0;
    double delay_ms = 260.0;
    double delay_feedback = 0.32;
    double delay_mix = 0.22;
    double volume_db = -8.0;

    bool operator==(const SynthSettings&) const = default;
};

// --- notes ---------------------------------------------------------------

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

// One sample of the arpeggiator. `step_frames` is a 16th note at the tempo; the gate holds for
// `gate_frames` of it and the rest is the envelope's release.
inline ArpTick arp_tick(ArpClock& clock, const ArpNotes& sequence, const SynthSettings& settings,
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

// --- voice ---------------------------------------------------------------

// Phase accumulator, polyBLEP on the discontinuous waveforms: a saw that does not alias and a
// square that does not either.
// simplify: sine and triangle stay naive (their harmonics fall off fast enough to be inaudible
// here). Upgrade path: polyBLAMP, integrating the square's BLEP twice.
class Oscillator {
public:
    void reset() noexcept { phase_ = 0.0; }
    void set_increment(double increment) noexcept { increment_ = increment; }

    float tick(Waveform waveform) noexcept {
        const double t = phase_;
        float sample = 0.f;
        switch (waveform) {
            case Waveform::Sine:
                sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * t));
                break;
            case Waveform::Saw:
                sample = static_cast<float>(2.0 * t - 1.0 - poly_blep(t, increment_));
                break;
            case Waveform::Square: {
                const double raw = t < 0.5 ? 1.0 : -1.0;
                const double wrapped = t + 0.5 >= 1.0 ? t - 0.5 : t + 0.5;
                sample = static_cast<float>(raw + poly_blep(t, increment_)
                                            - poly_blep(wrapped, increment_));
                break;
            }
            case Waveform::Triangle:
                sample = static_cast<float>(1.0 - 4.0 * std::abs(t - 0.5));
                break;
        }
        phase_ += increment_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return sample;
    }

private:
    static double poly_blep(double t, double dt) noexcept {
        if (dt <= 0.0) return 0.0;
        if (t < dt) {
            const double x = t / dt;
            return x + x - x * x - 1.0;
        }
        if (t > 1.0 - dt) {
            const double x = (t - 1.0) / dt;
            return x * x + x + x + 1.0;
        }
        return 0.0;
    }

    double phase_ = 0.0;
    double increment_ = 0.0;
};

// Topology-preserving state-variable low-pass (Zavalishin): the coefficients are prewarped, so
// sweeping the cutoff stays stable, and resonance is the filter's Q rather than a feedback gain.
class SvfLowpass {
public:
    void reset() noexcept {
        ic1_ = 0.0;
        ic2_ = 0.0;
    }

    void set(double sample_rate, double cutoff_hz, double q) noexcept {
        const double fc = std::clamp(cutoff_hz, 20.0, sample_rate * 0.5 * 0.95);
        const double g = std::tan(std::numbers::pi * fc / sample_rate);
        const double k = 1.0 / std::clamp(q, 0.5, 20.0);
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    float process(float x) noexcept {
        const double v3 = static_cast<double>(x) - ic2_;
        const double v1 = a1_ * ic1_ + a2_ * v3;
        const double v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0 * v1 - ic1_;
        ic2_ = 2.0 * v2 - ic2_;
        return static_cast<float>(v2);
    }

private:
    double a1_ = 1.0;
    double a2_ = 0.0;
    double a3_ = 0.0;
    double ic1_ = 0.0;
    double ic2_ = 0.0;
};

// Linear attack and release, landing on exactly zero rather than nearly zero: a released voice
// is silent, not a -80 dB drone.
class Envelope {
public:
    void reset() noexcept {
        level_ = 0.f;
        gate_ = false;
    }

    void set_times(double sample_rate, double attack_ms, double release_ms) noexcept {
        attack_step_ = static_cast<float>(1.0 / std::max(1.0, attack_ms * 1e-3 * sample_rate));
        release_step_ = static_cast<float>(1.0 / std::max(1.0, release_ms * 1e-3 * sample_rate));
    }

    void gate(bool open) noexcept { gate_ = open; }

    float tick() noexcept {
        if (gate_) {
            level_ = std::min(1.f, level_ + attack_step_);
            return level_;
        }
        level_ -= release_step_;
        if (level_ < 0.f) level_ = 0.f;
        return level_;
    }

    float level() const noexcept { return level_; }

private:
    float level_ = 0.f;
    float attack_step_ = 0.01f;
    float release_step_ = 0.001f;
    bool gate_ = false;
};

// A feedback delay with a fractional read head, so turning the time knob glides instead of
// clicking. Fixed storage: the longest time it holds is kCapacity / sample_rate.
class DelayLine {
public:
    static constexpr std::size_t kCapacity = 1u << 15;   // 0.68 s at 48 kHz

    void reset() noexcept { buffer_.fill(0.f); }

    void set_time(double sample_rate, double ms) noexcept {
        delay_ = std::clamp(ms * 1e-3 * sample_rate, 1.0, static_cast<double>(kCapacity - 2));
    }

    float process(float x, double feedback, double mix) noexcept {
        const float wet = read_tap();
        buffer_[write_] = x + wet * static_cast<float>(feedback);
        write_ = (write_ + 1) % kCapacity;
        return x + (wet - x) * static_cast<float>(mix);
    }

private:
    float read_tap() const noexcept {
        double pos = static_cast<double>(write_) - delay_;
        while (pos < 0.0) pos += static_cast<double>(kCapacity);
        const auto first = static_cast<std::size_t>(pos);
        const auto second = (first + 1) % kCapacity;
        const float frac = static_cast<float>(pos - static_cast<double>(first));
        return buffer_[first] + (buffer_[second] - buffer_[first]) * frac;
    }

    std::array<float, kCapacity> buffer_{};
    std::size_t write_ = 0;
    double delay_ = 4800.0;
};

// --- engine --------------------------------------------------------------

// One arpeggiated note at a time, through the whole chain: arpeggiator -> oscillator -> gate
// envelope -> resonant low-pass -> feedback delay -> volume -> soft clip.
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
