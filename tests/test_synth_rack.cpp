#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_synth_rack/rt_latest.hpp"
#include "../examples/model_synth_rack/spectrum.hpp"
#include "../examples/model_synth_rack/synth_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <type_traits>
#include <vector>

using namespace prism::synth;

// The handoffs carry values by copying their bytes.
static_assert(std::is_trivially_copyable_v<SynthSettings>);
static_assert(std::is_trivially_copyable_v<AnalyzerFrame>);

namespace {

std::vector<float> sine(double hz, std::size_t samples, double amplitude = 1.0) {
    std::vector<float> out(samples);
    for (std::size_t i = 0; i < samples; ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz
                                                         * static_cast<double>(i) / kSampleRate));
    return out;
}

float loudest(std::span<const float> samples) {
    float peak = 0.f;
    for (float sample : samples) peak = std::max(peak, std::abs(sample));
    return peak;
}

// RMS of a steady sine through the filter, skipping the first half as its transient.
double rms_through(double hz, double cutoff_hz) {
    SvfLowpass filter;
    filter.set(kSampleRate, cutoff_hz, 0.707);
    const std::vector<float> signal = sine(hz, 9600);
    double sum = 0.0;
    for (std::size_t i = 4800; i < signal.size(); ++i) {
        const float out = filter.process(signal[i]);
        sum += static_cast<double>(out) * static_cast<double>(out);
    }
    return std::sqrt(sum / 4800.0);
}

} // namespace

TEST_CASE("the FFT of a sine peaks at its own bin") {
    constexpr std::size_t bin = 64;
    std::array<float, kFftSize> re{};
    std::array<float, kFftSize> im{};
    for (std::size_t i = 0; i < kFftSize; ++i)
        re[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(bin)
                                            * static_cast<double>(i) / static_cast<double>(kFftSize)));

    const Fft<kFftSize> fft;
    fft.forward(re.data(), im.data());

    std::size_t peak_bin = 0;
    float peak = 0.f;
    for (std::size_t k = 1; k < kFftSize / 2; ++k) {
        const float magnitude = std::hypot(re[k], im[k]);
        if (magnitude > peak) {
            peak = magnitude;
            peak_bin = k;
        }
    }
    CHECK(peak_bin == bin);
    // A unit sine puts half its energy in its bin: N/2.
    CHECK(peak == doctest::Approx(static_cast<float>(kFftSize) / 2.f).epsilon(0.02));
}

TEST_CASE("the low-pass passes its passband and attenuates above the cutoff") {
    CHECK(rms_through(200.0, 500.0) == doctest::Approx(0.7071).epsilon(0.06));
    CHECK(rms_through(4000.0, 500.0) < 0.1);
    // Three octaves up from the cutoff is roughly -36 dB for two poles.
    CHECK(rms_through(4000.0, 500.0) < rms_through(2000.0, 500.0));
}

TEST_CASE("the envelope reaches exactly zero after its release") {
    Envelope envelope;
    envelope.set_times(kSampleRate, 1.0, 100.0);
    envelope.gate(true);
    for (int i = 0; i < 1000; ++i) envelope.tick();
    CHECK(envelope.level() == doctest::Approx(1.f));

    envelope.gate(false);
    const auto release_samples = static_cast<std::size_t>(0.1 * kSampleRate);
    for (std::size_t i = 0; i < release_samples + 2; ++i) envelope.tick();
    CHECK(envelope.level() == 0.f);

    for (int i = 0; i < 100; ++i) CHECK(envelope.tick() == 0.f);
}

TEST_CASE("the arpeggiator walks the chord in each pattern's order") {
    const ArpNotes sequence = arp_sequence(Chord::Cmaj7, 1);
    REQUIRE(sequence.count == 4);
    CHECK(sequence[0] == 60);   // C4
    CHECK(sequence[1] == 64);   // E4
    CHECK(sequence[2] == 67);   // G4
    CHECK(sequence[3] == 71);   // B4

    CHECK(arp_note_for(sequence, ArpPattern::Up, 0) == 60);
    CHECK(arp_note_for(sequence, ArpPattern::Up, 1) == 64);
    CHECK(arp_note_for(sequence, ArpPattern::Up, 4) == 60);   // wraps at the top
    CHECK(arp_note_for(sequence, ArpPattern::Down, 0) == 71);
    CHECK(arp_note_for(sequence, ArpPattern::Down, 3) == 60);
    CHECK(arp_note_for(sequence, ArpPattern::UpDown, 0) == 60);
    CHECK(arp_note_for(sequence, ArpPattern::UpDown, 3) == 71);
    CHECK(arp_note_for(sequence, ArpPattern::UpDown, 4) == 67);   // turns around, no repeat
    CHECK(arp_note_for(sequence, ArpPattern::UpDown, 5) == 64);
    CHECK(arp_note_for(sequence, ArpPattern::UpDown, 6) == 60);
    // Random is a function of the step, so the same step always picks the same note.
    for (std::size_t step = 0; step < 32; ++step)
        CHECK(arp_note_for(sequence, ArpPattern::Random, step)
              == arp_note_for(sequence, ArpPattern::Random, step));
}

TEST_CASE("the octave span repeats the chord an octave up") {
    const ArpNotes two = arp_sequence(Chord::Cmaj7, 2);
    REQUIRE(two.count == 8);
    CHECK(two[4] == 72);
    CHECK(two[7] == 83);
    CHECK(arp_sequence(Chord::Cmaj7, 3).count == 12);
    CHECK(arp_sequence(Chord::Am9, 1)[1] == 60);   // A3 C4 E4 G4
}

TEST_CASE("a closed gate silences its step while the clock keeps running") {
    SynthSettings settings{};
    settings.chord = Chord::Cmaj7;
    settings.pattern = ArpPattern::Up;
    settings.step_mask = static_cast<std::uint16_t>(~0x0002u);   // step 2 off

    const ArpNotes sequence = arp_sequence(settings.chord, 1);
    constexpr std::size_t step_frames = 10;
    constexpr std::size_t gate_frames = 8;
    ArpClock clock{};
    std::array<bool, kSteps> heard{};

    for (std::size_t i = 0; i < 2 * step_frames; ++i) {
        const ArpTick tick = arp_tick(clock, sequence, settings, step_frames, gate_frames);
        CHECK(tick.step == i / step_frames);
        if (tick.sounding) heard[tick.step] = true;
    }
    CHECK(heard[0]);
    CHECK_FALSE(heard[1]);

    // The clock is past both steps and back at the top of the bar's second half.
    CHECK(clock.step == 2);
    CHECK(clock.frame == 0);
}

TEST_CASE("the step clock follows the tempo") {
    CHECK(step_frames_for(kSampleRate, 120.0) == 6000);   // 120 BPM: one 16th is 125 ms
    CHECK(step_frames_for(kSampleRate, 60.0) == 12000);
    CHECK(step_frames_for(kSampleRate, 0.0) > 0);         // never a zero-length step
}

TEST_CASE("the latest-value handoff returns the newest value, whole") {
    RtLatest<SynthSettings> latest{};
    CHECK(latest.read() == SynthSettings{});

    SynthSettings first{};
    first.tempo_bpm = 120.0;
    first.volume_db = -12.0;
    latest.publish(first);
    CHECK(latest.read() == first);
    // Reading without a publish in between hands back the same value, not a stale slot.
    CHECK(latest.read() == first);

    SynthSettings second = first;
    second.cutoff_hz = 900.0;
    latest.publish(second);
    CHECK(latest.read() == second);

    SynthSettings third = second;
    third.tempo_bpm = 140.0;
    latest.publish(third);
    CHECK(latest.read() == third);
    CHECK(latest.read() == third);
}

TEST_CASE("the sample ring hands back the newest samples") {
    SampleRing ring{1024};
    CHECK(ring.capacity() == 1024);

    std::array<float, 8> window{};
    CHECK(ring.read_newest(window) == 0);

    std::array<float, 64> block{};
    for (std::size_t i = 0; i < block.size(); ++i) block[i] = static_cast<float>(i);
    ring.write(block);

    REQUIRE(ring.read_newest(window) == 8);
    for (std::size_t i = 0; i < window.size(); ++i)
        CHECK(window[i] == doctest::Approx(static_cast<float>(64 - 8 + i)));

    // More than the capacity: the oldest samples are gone and the window ends at the last one.
    std::array<float, 2000> more{};
    for (std::size_t i = 0; i < more.size(); ++i) more[i] = static_cast<float>(i);
    ring.write(more);

    REQUIRE(ring.read_newest(window) == 8);
    CHECK(window[0] == doctest::Approx(1992.f));
    CHECK(window[7] == doctest::Approx(1999.f));
}

TEST_CASE("the engine is silent while it is off and audible while it is on") {
    SynthEngine engine;
    std::vector<float> block(1024, 0.f);

    SynthSettings settings{};
    engine.render(settings, block);
    CHECK(loudest(block) == 0.f);

    settings.powered = true;
    engine.render(settings, block);
    CHECK(loudest(block) > 0.01f);

    settings.powered = false;
    engine.render(settings, block);
    CHECK(loudest(block) == 0.f);
}

TEST_CASE("a tone lands in the display band its frequency maps to") {
    SpectrumAnalyzer analyzer{kSampleRate};
    const AnalyzerFrame shot = analyzer.analyze(sine(1000.0, kFftSize));

    std::size_t loudest_band = 0;
    for (std::size_t band = 1; band < kBandCount; ++band)
        if (shot.spectrum_db[band] > shot.spectrum_db[loudest_band]) loudest_band = band;

    const double hz = SpectrumMap::band_hz(loudest_band);
    CHECK(hz > 800.0);
    CHECK(hz < 1250.0);
    CHECK(shot.peak_db > -3.f);
    CHECK(shot.peak_db < 1.f);
}

TEST_CASE("the scope window starts on a rising zero crossing") {
    std::array<float, 64> samples{};
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = (i % 16 < 8) ? -1.f : 1.f;

    // The newest crossing that still leaves a full window after it.
    CHECK(trigger_start(samples, 16) == 40);
    // Too short for a window: start at the beginning.
    CHECK(trigger_start(std::span<const float>(samples.data(), 8), 16) == 0);
}
