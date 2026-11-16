#include <prism/audio/output.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <vector>

namespace prism::audio {

namespace {
using clock = std::chrono::steady_clock;

// Sleeps in short slices so a stop request is noticed at once; true when one came.
bool sleep_until(const std::stop_token& stop, clock::time_point deadline) {
    constexpr auto slice = std::chrono::milliseconds{5};
    while (clock::now() < deadline) {
        if (stop.stop_requested()) return true;
        std::this_thread::sleep_for(slice);
    }
    return stop.stop_requested();
}

float budget_ms(const Format& f) {
    return f.sample_rate <= 0 ? 0.f
                              : 1000.f * static_cast<float>(f.block_frames) / static_cast<float>(f.sample_rate);
}
} // namespace

// Written by the one thread that renders, read by anyone: the atomics are the published load.
class LoadMeter {
public:
    explicit LoadMeter(int sample_rate) : sample_rate_(sample_rate) {}

    // A one-second window of audio, not of wall time: no extra clock read per block, and the
    // numbers still mean "what a second of audio cost".
    void record(float milliseconds, int frames) {
        window_max_ms_ = std::max(window_max_ms_, milliseconds);
        window_sum_ms_ += milliseconds;
        window_calls_ += 1;
        window_frames_ += frames;
        max_ms_.store(window_max_ms_, std::memory_order_relaxed);
        mean_ms_.store(window_sum_ms_ / static_cast<float>(window_calls_), std::memory_order_relaxed);
        calls_.store(window_calls_, std::memory_order_relaxed);
        if (window_frames_ >= sample_rate_) {
            window_max_ms_ = window_sum_ms_ = 0.f;
            window_calls_ = 0;
            window_frames_ = 0;
        }
    }

    Load read(float budget) const {
        return {.max_ms = max_ms_.load(std::memory_order_relaxed),
                .mean_ms = mean_ms_.load(std::memory_order_relaxed),
                .calls = calls_.load(std::memory_order_relaxed),
                .budget_ms = budget};
    }

private:
    int sample_rate_;
    std::atomic<float> max_ms_{0.f};
    std::atomic<float> mean_ms_{0.f};
    std::atomic<std::uint32_t> calls_{0};
    float window_max_ms_ = 0.f;
    float window_sum_ms_ = 0.f;
    int window_frames_ = 0;
    std::uint32_t window_calls_ = 0;
};

struct Output::Impl {
    Impl(Format f, RenderFn r)
        : format(f), render(std::move(r)), meter(f.sample_rate),
          scratch(static_cast<std::size_t>(kMaxBlockFrames) * static_cast<std::size_t>(std::max(1, f.channels))) {}

    ~Impl() {
        if (stream) SDL_DestroyAudioStream(stream);   // stops SDL's audio thread, closes the device
        timer = std::jthread{};                       // requests a stop, then joins
        if (sdl_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    void start() {
        open_device();
        if (!stream) start_timer();
    }

    void open_device() {
        sdl_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
        if (!sdl_audio) {
            error = SDL_GetError();
            return;
        }
        const SDL_AudioSpec spec{.format = SDL_AUDIO_F32, .channels = format.channels, .freq = format.sample_rate};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::sdl_callback, this);
        if (!stream) {
            error = SDL_GetError();
        } else if (!SDL_ResumeAudioStreamDevice(stream)) {
            error = SDL_GetError();
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
    }

    static void SDLCALL sdl_callback(void* self, SDL_AudioStream* s, int additional_bytes, int) {
        static_cast<Impl*>(self)->pump(s, additional_bytes);
    }

    // SDL asks for bytes in our format: render in blocks of at most kMaxBlockFrames into storage
    // that never grows here, then hand them over.
    void pump(SDL_AudioStream* s, int additional_bytes) {
        const int frame_bytes = format.channels * static_cast<int>(sizeof(float));
        if (frame_bytes <= 0) return;
        for (int remaining = additional_bytes / frame_bytes; remaining > 0;) {
            const int frames = std::min(remaining, kMaxBlockFrames);
            render_block(frames);
            SDL_PutAudioStreamData(s, scratch.data(), frames * frame_bytes);
            remaining -= frames;
        }
    }

    void render_block(int frames) {
        const auto began = clock::now();
        render(std::span<float>{scratch.data(), static_cast<std::size_t>(frames * format.channels)});
        meter.record(std::chrono::duration<float, std::milli>(clock::now() - began).count(), frames);
    }

    void start_timer() {
        try {
            timer = std::jthread([this](std::stop_token stop) { run_timer(stop); });
        } catch (const std::exception&) {
            // No thread left to render on: the app still runs, it just renders nothing.
        }
    }

    void run_timer(const std::stop_token& stop) {
        const auto block = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(
            static_cast<double>(format.block_frames) / static_cast<double>(format.sample_rate)));
        auto deadline = clock::now();
        while (!stop.stop_requested()) {
            render_block(std::min(format.block_frames, kMaxBlockFrames));
            deadline = std::max(deadline + block, clock::now());   // a slow block: no catching up
            if (sleep_until(stop, deadline)) return;
        }
    }

    Format format;
    RenderFn render;
    LoadMeter meter;
    std::vector<float> scratch;
    std::string error;
    bool sdl_audio = false;
    SDL_AudioStream* stream = nullptr;
    std::jthread timer;
};

Output::Output(Format format, RenderFn render) : impl_(std::make_unique<Impl>(format, std::move(render))) {
    impl_->start();
}
Output::~Output() = default;
bool Output::playing() const { return impl_->stream != nullptr; }
const std::string& Output::error() const { return impl_->error; }
Format Output::format() const { return impl_->format; }
Load Output::load() const { return impl_->meter.read(budget_ms(impl_->format)); }

} // namespace prism::audio
