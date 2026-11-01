#pragma once

// WebGPU renderer: draws a SceneSnapshot into a WebGPU texture. Written against the standard
// webgpu.h only (wgpu-native on desktop; the same API in browsers), so it can serve both.
// See doc/design/second-backend.md.

#include <prism/core/types.hpp>
#include <prism/render/frame_cache.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/render/text_bitmap.hpp>

#include <webgpu/webgpu.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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

using render::TextBitmap;
using render::TextRasterizer;

// What one render draws, back to front: snapshots or single draw lists, each in its own band of
// the target starting `top` points from its top edge (a window's content below its title bar,
// then the title bar itself). Exactly one of `snapshot` and `list` is set.
struct Layer {
    const render::SceneSnapshot* snapshot = nullptr;
    const render::DrawList* list = nullptr;
    float top = 0.f;
};

class WgpuRenderer {
public:
    WgpuRenderer(const WgpuContext& ctx, WGPUTextureFormat target_format, TextRasterizer text = {});
    ~WgpuRenderer();
    WgpuRenderer(const WgpuRenderer&) = delete;
    WgpuRenderer& operator=(const WgpuRenderer&) = delete;

    // Clears the target and draws the layers. `scale` is device pixels per point: the target
    // is width_px x height_px pixels, the layers are in points.
    void render(std::span<const Layer> layers, WGPUTextureView target, int width_px, int height_px,
                float scale, core::Color clear);
    void render(const render::SceneSnapshot& snap, WGPUTextureView target, int width_px, int height_px,
                float scale, core::Color clear) {
        Layer whole{&snap, nullptr, 0.f};
        render(std::span{&whole, 1}, target, width_px, height_px, scale, clear);
    }

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
    WGPUBindGroupLayout uniform_layout_ = nullptr;
    struct Uniform {
        WGPUBuffer buffer = nullptr;
        WGPUBindGroup group = nullptr;
    };
    std::vector<Uniform> uniforms_;   // one per layer: each has its own viewport size
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
std::vector<std::uint8_t> render_to_pixels(const WgpuContext& ctx, std::span<const Layer> layers,
                                           int width_px, int height_px, float scale, core::Color clear,
                                           TextRasterizer text = {});

} // namespace prism::backends
