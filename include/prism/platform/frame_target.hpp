#pragma once

// What a framebuffer target gives the frame runtime (platform/frame_app.hpp): pixels to draw
// into and a touch panel to poll. A port implements both (the STM32 board: the LTDC back buffer
// and its touch controller).

#include <prism/core/types.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace prism::platform {

// What the render thread draws into and presents.
struct FrameSink {
    virtual std::span<std::uint32_t> back() = 0; // ARGB8888 pixels, stride = width
    virtual void present() = 0;                  // flip to what was drawn into back()
    virtual core::Size size() const = 0;         // in pixels: back() holds width x height
    virtual ~FrameSink() = default;
};

// The touch panel, polled: the finger's position in window/panel pixels, or nullopt
// when no finger is down.
struct TouchSource {
    virtual std::optional<core::Point> read() = 0;
    virtual ~TouchSource() = default;
};

} // namespace prism::platform
