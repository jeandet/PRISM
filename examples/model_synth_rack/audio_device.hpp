#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// The only file that includes SDL: a playback device that calls a render function from SDL's
// audio thread, plus the bookkeeping that says what that callback costs.
//
// When no device can be opened (no sound card, a headless machine, a browser that refuses to
// start an AudioContext) the same render function is driven by a timer thread at the block rate
// instead. Exactly one thread renders in both cases, so the analyzer and the budget readout stay
// live; you just do not hear it. `error()` says why.
namespace prism::synth {

struct CallbackLoad {
    float max_ms = 0.f;      // over the last second of audio
    float mean_ms = 0.f;
    std::uint32_t calls = 0;
    float budget_ms = 0.f;   // how long one block of audio takes to play
};

class AudioDevice {
public:
    static constexpr int kMaxFrames = 2048;   // the largest block one callback renders

    struct Config {
        int sample_rate = 48000;
        int channels = 2;
        int buffer_frames = 512;
    };

    // Renders `frames` frames of interleaved float samples. Runs on the audio thread.
    using RenderFn = std::function<void(float* interleaved, int frames)>;

    AudioDevice() = default;

    // The format is fixed at construction, before any thread exists: the audio thread reads it,
    // and nothing writes it afterwards.
    explicit AudioDevice(Config config) : config_(config) {}

    ~AudioDevice() { close(); }

    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    // Opens the default playback device and starts rendering. Returns false only when there is
    // nothing to render with; a missing device is not an error the caller has to handle.
    bool open(RenderFn render) {
        close();
        if (!render) return false;
        render_ = std::move(render);
        scratch_.assign(static_cast<std::size_t>(kMaxFrames)
                            * static_cast<std::size_t>(std::max(1, config_.channels)),
                        0.f);
        error_.clear();

        audio_inited_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
        if (!audio_inited_) {
            error_ = SDL_GetError();
        } else {
            const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                                     .channels = config_.channels,
                                     .freq = config_.sample_rate};
            stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                               &AudioDevice::sdl_callback, this);
            if (stream_ == nullptr) {
                error_ = SDL_GetError();
            } else if (!SDL_ResumeAudioStreamDevice(stream_)) {
                error_ = SDL_GetError();
                SDL_DestroyAudioStream(stream_);   // also closes the device
                stream_ = nullptr;
            }
        }

        if (stream_ == nullptr) start_fallback();
        open_ = true;
        return true;
    }

    void close() {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);   // stops SDL's audio thread, closes the device
            stream_ = nullptr;
        }
        fallback_ = std::jthread{};            // requests a stop, then joins
        if (audio_inited_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_inited_ = false;
        }
        render_ = nullptr;
        open_ = false;
    }

    bool is_open() const { return open_; }

    // Whether a real device is playing; false means the timer thread is driving the render.
    bool is_playing() const { return stream_ != nullptr; }

    const std::string& error() const { return error_; }

    Config config() const { return config_; }

    float budget_ms() const {
        if (config_.sample_rate <= 0) return 0.f;
        return 1000.f * static_cast<float>(config_.buffer_frames)
               / static_cast<float>(config_.sample_rate);
    }

    CallbackLoad load() const {
        return {.max_ms = max_ms_.load(std::memory_order_relaxed),
                .mean_ms = mean_ms_.load(std::memory_order_relaxed),
                .calls = calls_.load(std::memory_order_relaxed),
                .budget_ms = budget_ms()};
    }

private:
    using clock = std::chrono::steady_clock;

    static void SDLCALL sdl_callback(void* userdata, SDL_AudioStream* stream,
                                     int additional_amount, int /*total_amount*/) {
        static_cast<AudioDevice*>(userdata)->pump(stream, additional_amount);
    }

    // SDL asks for bytes in our format; render in blocks of at most kMaxFrames into storage that
    // never grows here, then hand them over.
    void pump(SDL_AudioStream* stream, int additional_bytes) {
        const int frame_bytes = config_.channels * static_cast<int>(sizeof(float));
        if (frame_bytes <= 0) return;
        int remaining = additional_bytes / frame_bytes;
        while (remaining > 0) {
            const int frames = std::min(remaining, kMaxFrames);
            render_and_measure(frames);
            SDL_PutAudioStreamData(stream, scratch_.data(), frames * frame_bytes);
            remaining -= frames;
        }
    }

    void render_and_measure(int frames) {
        const auto began = clock::now();
        render_(scratch_.data(), frames);
        const auto elapsed = std::chrono::duration<double, std::milli>(clock::now() - began).count();
        record(static_cast<float>(elapsed), frames);
    }

    // A one-second window of audio, not of wall time: no extra clock read on the audio thread,
    // and the numbers still mean "what a second of audio cost".
    void record(float milliseconds, int frames) {
        window_max_ms_ = std::max(window_max_ms_, milliseconds);
        window_sum_ms_ += milliseconds;
        window_calls_ += 1;
        window_frames_ += frames;
        max_ms_.store(window_max_ms_, std::memory_order_relaxed);
        mean_ms_.store(window_sum_ms_ / static_cast<float>(window_calls_), std::memory_order_relaxed);
        calls_.store(window_calls_, std::memory_order_relaxed);
        if (window_frames_ >= config_.sample_rate) {
            window_max_ms_ = 0.f;
            window_sum_ms_ = 0.f;
            window_calls_ = 0;
            window_frames_ = 0;
        }
    }

    void start_fallback() {
        try {
            fallback_ = std::jthread([this](std::stop_token stop) { run_fallback(stop); });
        } catch (const std::exception&) {
            // No thread left to render on: the UI still works, it just has nothing to show.
        }
    }

    void run_fallback(const std::stop_token& stop) {
        const auto block = std::chrono::duration<double>(static_cast<double>(config_.buffer_frames)
                                                         / static_cast<double>(config_.sample_rate));
        auto deadline = clock::now();
        while (!stop.stop_requested()) {
            render_and_measure(config_.buffer_frames);
            deadline += std::chrono::duration_cast<clock::duration>(block);
            const auto now = clock::now();
            if (deadline < now) deadline = now;   // a slow block: do not try to catch up
            if (sleep_until(stop, deadline)) return;
        }
    }

    // Sleeps in short slices so a stop request is noticed at once, and reports whether one came.
    static bool sleep_until(const std::stop_token& stop, clock::time_point deadline) {
        constexpr auto slice = std::chrono::milliseconds{5};
        while (clock::now() < deadline) {
            if (stop.stop_requested()) return true;
            std::this_thread::sleep_for(slice);
        }
        return stop.stop_requested();
    }

    Config config_{};
    RenderFn render_{};
    SDL_AudioStream* stream_ = nullptr;
    std::jthread fallback_{};
    std::vector<float> scratch_{};
    bool audio_inited_ = false;
    bool open_ = false;
    std::string error_{};

    std::atomic<float> max_ms_{0.f};
    std::atomic<float> mean_ms_{0.f};
    std::atomic<std::uint32_t> calls_{0};
    float window_max_ms_ = 0.f;
    float window_sum_ms_ = 0.f;
    int window_frames_ = 0;
    std::uint32_t window_calls_ = 0;
};

} // namespace prism::synth
