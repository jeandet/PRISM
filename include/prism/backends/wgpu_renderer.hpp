#pragma once

// WebGPU renderer: draws a SceneSnapshot into a WebGPU texture. Written against the standard
// webgpu.h only (wgpu-native on desktop; the same API in browsers), so it can serve both.
// See doc/design/second-backend.md.

#include <prism/core/types.hpp>
#include <prism/render/frame_cache.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <webgpu/webgpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism::backends {

// Instance, adapter, device and queue. One per process is enough; windows share it. Like the
// renderers, used from the render thread only.
class WgpuContext {
public:
    // Blocks until the device is ready. nullptr when the system offers no adapter.
    static std::unique_ptr<WgpuContext> create();
    ~WgpuContext();
    WgpuContext(const WgpuContext&) = delete;
    WgpuContext& operator=(const WgpuContext&) = delete;

    WGPUInstance instance() const { return instance_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    // Blocks until the queue's submitted work is done (desktop only).
    void wait_idle() const;

private:
    WgpuContext() = default;
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
};

// A string rasterized in white, coverage in alpha: RGBA8, rows tightly packed, top row first.
// The renderer tints it with the text colour, so one bitmap serves every colour.
struct TextBitmap {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
// Rasterizes `text` at `px_size` device pixels in the font for `role`; nullopt draws nothing.
// The backend supplies it (the SDL setup through SDL_ttf), so the renderer has no font code and
// text is drawn with the same fonts the measurer measures with.
using TextRasterizer = std::function<std::optional<TextBitmap>(std::string_view text, float px_size, render::FontRole)>;

class WgpuRenderer {
public:
    WgpuRenderer(const WgpuContext& ctx, WGPUTextureFormat target_format, TextRasterizer text = {});
    ~WgpuRenderer();
    WgpuRenderer(const WgpuRenderer&) = delete;
    WgpuRenderer& operator=(const WgpuRenderer&) = delete;

    // Clears the target and draws the snapshot. `scale` is device pixels per point: the target
    // is width_px x height_px pixels, the snapshot is in points.
    void render(const render::SceneSnapshot& snap, WGPUTextureView target, int width_px, int height_px,
                float scale, core::Color clear);

    struct Texture {
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        WGPUBindGroup group = nullptr;
        int width = 0, height = 0;
    };

private:
    Texture upload(int width, int height, const std::uint8_t* rgba);
    const Texture* text_texture(const render::TextCmd& cmd, float scale);
    const Texture* image_texture(const render::ImageCmd& cmd);

    const WgpuContext& ctx_;
    TextRasterizer rasterize_;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout texture_layout_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBuffer uniforms_ = nullptr;
    WGPUBindGroup uniform_group_ = nullptr;
    WGPUBuffer vertices_ = nullptr;
    std::uint64_t vertex_capacity_ = 0;   // bytes
    Texture white_;                        // what shapes sample
    render::FrameCache<Texture> text_cache_;
    render::FrameCache<Texture> image_cache_;
    std::string key_;                      // reused: building a cache key must not allocate per draw
};

// Renders offscreen and reads the pixels back: RGBA8, rows tightly packed, top row first.
// For tests and image export.
std::vector<std::uint8_t> render_to_pixels(const WgpuContext& ctx, const render::SceneSnapshot& snap,
                                           int width_px, int height_px, float scale, core::Color clear,
                                           TextRasterizer text = {});

} // namespace prism::backends
