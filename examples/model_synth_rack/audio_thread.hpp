#pragma once

#include <prism/prism.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <span>
#include <string>
#include <thread>

#include "dsp/audio_device.hpp"
#include "dsp/engine.hpp"
#include "dsp/rt_latest.hpp"
#include "dsp/spectrum.hpp"

// The audio side of the app, and the only part of it with threads in it. It never reads a Field:
// settings come in through `publish()` (an RtLatest the callback copies per block) and display
// frames go out through the `core::Shared<AnalyzerFrame>` it was handed.
namespace prism::synth {

inline constexpr int kDeviceRate = static_cast<int>(kSampleRate);
inline constexpr int kBufferFrames = 512;
inline constexpr int kChannels = 2;
inline constexpr std::chrono::milliseconds kAnalysisPeriod{33};   // ~30 frames per second
inline constexpr std::chrono::milliseconds kSleepSlice{5};

// Sleeps in slices so a stop request is noticed while waiting; true when one arrived.
inline bool sleep_slices(const std::stop_token& stop, std::chrono::milliseconds total) {
    for (auto slept = std::chrono::milliseconds{0}; slept < total; slept += kSleepSlice) {
        if (stop.stop_requested()) return true;
        std::this_thread::sleep_for(kSleepSlice);
    }
    return stop.stop_requested();
}

class AudioThread {
public:
    explicit AudioThread(core::Shared<AnalyzerFrame>& frames) : frames_(frames) {}
    AudioThread(const AudioThread&) = delete;
    AudioThread& operator=(const AudioThread&) = delete;

    void publish(const SynthSettings& settings) { to_audio_.publish(settings); }

    // No audio thread exists before this call, so the engine can be reset here.
    void open() {
        engine_.reset();
        device_.open(
            [this](float* interleaved, int frames) { render(interleaved, frames); });
    }

    void close() { device_.close(); }

    bool is_open() const { return device_.is_open(); }

    std::string status_text() const {
        const auto config = device_.config();
        if (device_.is_playing())
            return fmt::format("audio {} Hz · {} frame block · {:.2f} ms per block",
                               config.sample_rate, config.buffer_frames, device_.budget_ms());
        return fmt::format("no audio device ({}) — still rendering, silently", device_.error());
    }

    // Headless capture: run the same render path offline and time it here, since a capture run
    // has no callback to measure.
    AnalyzerFrame render_offline(int blocks) {
        std::array<float, kChannels * AudioDevice::kMaxFrames> interleaved{};
        float total_ms = 0.f;
        float slowest_ms = 0.f;
        for (int block = 0; block < blocks; ++block) {
            const auto began = std::chrono::steady_clock::now();
            render(interleaved.data(), AudioDevice::kMaxFrames);
            const float elapsed =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - began)
                    .count();
            total_ms += elapsed;
            slowest_ms = std::max(slowest_ms, elapsed);
        }
        AnalyzerFrame shot = make_frame(newest_samples());
        shot.callback_mean_ms = total_ms / static_cast<float>(blocks);
        shot.callback_max_ms = slowest_ms;
        return shot;
    }

    // One display frame from the newest samples, published only when it differs from the last
    // one, so an idle synth does not wake the window thirty times a second.
    void analyse_once() {
        const AnalyzerFrame shot = make_frame(newest_samples());
        if (shot == last_shot_) return;
        last_shot_ = shot;
        frames_.set(shot);
    }

    // The analysis thread: newest samples in, one display frame out, ~30 times per second.
    void run_analysis(const std::stop_token& stop) {
        while (!stop.stop_requested()) {
            analyse_once();
            if (sleep_slices(stop, kAnalysisPeriod)) return;
        }
    }

private:
    // The audio callback: one settings snapshot in, one block of interleaved samples out. Never
    // blocks, never allocates, never touches a Field.
    void render(float* interleaved, int frames) {
        const SynthSettings settings = to_audio_.read();
        if (!(settings == last_settings_)) {
            last_settings_ = settings;
            telemetry_.parameter_updates.fetch_add(1, std::memory_order_relaxed);
        }

        const auto count = static_cast<std::size_t>(frames);
        const std::span<float> mono{mono_scratch_.data(), count};
        const SynthEngine::Report report = engine_.render(settings, mono);

        for (std::size_t frame = 0; frame < count; ++frame) {
            interleaved[kChannels * frame] = mono[frame];
            interleaved[kChannels * frame + 1] = mono[frame];
        }
        samples_.write(mono);
        telemetry_.step.store(static_cast<std::uint32_t>(report.step), std::memory_order_relaxed);
        telemetry_.note.store(report.note, std::memory_order_relaxed);
        telemetry_.sounding.store(report.sounding, std::memory_order_relaxed);
    }

    // The newest window of rendered samples, oldest first. Fewer only before anything played.
    std::span<const float> newest_samples() {
        const std::size_t got = samples_.read_newest(scratch_window_);
        return {scratch_window_.data(), got};
    }

    AnalyzerFrame make_frame(std::span<const float> newest) {
        AnalyzerFrame shot = spectrum_.analyze(newest);
        const CallbackLoad load = device_.load();
        shot.callback_max_ms = load.max_ms;
        shot.callback_mean_ms = load.mean_ms;
        shot.buffer_frames = static_cast<std::uint32_t>(device_.config().buffer_frames);
        shot.sample_rate = device_.config().sample_rate;
        shot.parameter_updates = telemetry_.parameter_updates.load(std::memory_order_relaxed);
        shot.step = telemetry_.step.load(std::memory_order_relaxed);
        shot.note = telemetry_.note.load(std::memory_order_relaxed);
        shot.sounding = telemetry_.sounding.load(std::memory_order_relaxed);
        return shot;
    }

    struct Telemetry {
        std::atomic<std::uint32_t> parameter_updates{0};
        std::atomic<std::uint32_t> step{0};
        std::atomic<int> note{-1};
        std::atomic<bool> sounding{false};
    };

    core::Shared<AnalyzerFrame>& frames_;
    RtLatest<SynthSettings> to_audio_{};
    SampleRing samples_{1u << 14};
    Telemetry telemetry_{};
    AudioDevice device_{{kDeviceRate, kChannels, kBufferFrames}};
    SynthEngine engine_{kSampleRate};
    SpectrumAnalyzer spectrum_{kSampleRate};
    std::array<float, AudioDevice::kMaxFrames> mono_scratch_{};
    std::array<float, kFftSize> scratch_window_{};

    // Audio-thread state, owned by the render callback.
    SynthSettings last_settings_{};

    // Analysis state, owned by whichever thread analyses: the analyzer thread, or the logic
    // thread when the platform has no worker to spare.
    AnalyzerFrame last_shot_{};
};

} // namespace prism::synth
