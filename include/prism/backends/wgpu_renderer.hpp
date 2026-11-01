#pragma once

// WebGPU renderer: draws a SceneSnapshot into a WebGPU texture. Written against the standard
// webgpu.h only (wgpu-native on desktop; the same API in browsers), so it can serve both.
// See doc/design/second-backend.md.

#include <prism/core/types.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <webgpu/webgpu.h>

#include <cstdint>
#include <memory>
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

class WgpuRenderer {
public:
    WgpuRenderer(const WgpuContext& ctx, WGPUTextureFormat target_format);
    ~WgpuRenderer();
    WgpuRenderer(const WgpuRenderer&) = delete;
    WgpuRenderer& operator=(const WgpuRenderer&) = delete;

    // Clears the target and draws the snapshot. `scale` is device pixels per point: the target
    // is width_px x height_px pixels, the snapshot is in points.
    void render(const render::SceneSnapshot& snap, WGPUTextureView target, int width_px, int height_px,
                float scale, core::Color clear);

private:
    const WgpuContext& ctx_;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBuffer uniforms_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUBuffer vertices_ = nullptr;
    std::uint64_t vertex_capacity_ = 0;   // bytes
};

// Renders offscreen and reads the pixels back: RGBA8, rows tightly packed, top row first.
// For tests and image export.
std::vector<std::uint8_t> render_to_pixels(const WgpuContext& ctx, const render::SceneSnapshot& snap,
                                           int width_px, int height_px, float scale, core::Color clear);

} // namespace prism::backends
