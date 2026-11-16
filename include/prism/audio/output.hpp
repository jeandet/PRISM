#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

// Sound out: a playback device that calls a render function on its own thread. The render
// function fills one block of interleaved float samples; it runs on a real-time thread, so it
// must not block, allocate or touch a Field (hand it values through a core::Latest).
//
// When no device opens (no sound card, a headless machine, a browser that has not started audio
// yet) a timer thread calls the same function at the block rate instead: exactly one thread
// renders either way, so whatever reads the samples stays live. playing() and error() say which.
// The SDL implementation lives in the software backend's library (src/backends/sdl_audio.cpp).
namespace prism::audio {

inline constexpr int kMaxBlockFrames = 2048;   // the largest block one call renders

struct Format {
    int sample_rate = 48000;
    int channels = 2;
    int block_frames = 512;   // what the timer renders per call; a device may ask for other sizes
};

// What the render function costs, over the last second of audio.
struct Load {
    float max_ms = 0.f;
    float mean_ms = 0.f;
    std::uint32_t calls = 0;
    float budget_ms = 0.f;   // how long one block takes to play
};

// `interleaved` holds frames * channels samples, frames <= kMaxBlockFrames.
using RenderFn = std::function<void(std::span<float> interleaved)>;

class Output {
public:
    // Starts rendering at once, on a device or, failing that, on the timer.
    Output(Format format, RenderFn render);
    ~Output();   // stops the thread that renders; render is never called after it returns

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    [[nodiscard]] bool playing() const;              // false: the timer renders, silently
    [[nodiscard]] const std::string& error() const;  // why no device opened
    [[nodiscard]] Format format() const;
    [[nodiscard]] Load load() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace prism::audio
