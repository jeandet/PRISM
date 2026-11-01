#include <prism/backends/wgpu_renderer.hpp>

#include <prism/render/tessellate.hpp>

#include <webgpu/wgpu.h>   // wgpuDevicePoll: wgpu-native's blocking wait (desktop only)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace prism::backends {

namespace {

WGPUStringView sv(std::string_view s) { return WGPUStringView{s.data(), s.size()}; }

// Waits for an async request by processing instance events (desktop: returns promptly).
template <typename Done>
void process_until(WGPUInstance inst, Done done) {
    for (int i = 0; i < 100000 && !done(); ++i) wgpuInstanceProcessEvents(inst);
}

constexpr std::string_view shader = R"(
struct Uniforms { viewport: vec2f };   // the target's size in points
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};

@vertex fn vs(@location(0) pos: vec2f, @location(1) color: vec4f) -> VertexOut {
    var out: VertexOut;
    out.position = vec4f(pos.x / u.viewport.x * 2.0 - 1.0, 1.0 - pos.y / u.viewport.y * 2.0, 0.0, 1.0);
    out.color = color;
    return out;
}

@fragment fn fs(in: VertexOut) -> @location(0) vec4f { return in.color; }
)";

struct ScissorPx {
    std::uint32_t x, y, w, h;
};

// The clip in device pixels, rounded outward (as the SDL backend does), inside the target.
ScissorPx scissor_of(const std::optional<core::Rect>& clip, int w, int h, float scale) {
    if (!clip) return {0, 0, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
    auto px = [&](float v, int limit, bool up) {
        float s = up ? std::ceil(v * scale) : std::floor(v * scale);
        return std::clamp(static_cast<int>(s), 0, limit);
    };
    int x0 = px(clip->origin.x.raw(), w, false), y0 = px(clip->origin.y.raw(), h, false);
    int x1 = px(clip->origin.x.raw() + clip->extent.w.raw(), w, true);
    int y1 = px(clip->origin.y.raw() + clip->extent.h.raw(), h, true);
    return {static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0),
            static_cast<std::uint32_t>(std::max(0, x1 - x0)), static_cast<std::uint32_t>(std::max(0, y1 - y0))};
}

} // namespace

// ---- Context ----

std::unique_ptr<WgpuContext> WgpuContext::create() {
    std::unique_ptr<WgpuContext> ctx{new WgpuContext};
    ctx->instance_ = wgpuCreateInstance(nullptr);
    if (!ctx->instance_) return nullptr;

    bool adapter_done = false;
    WGPURequestAdapterCallbackInfo acb{};
    acb.mode = WGPUCallbackMode_AllowProcessEvents;
    acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView, void* self, void* done) {
        if (status == WGPURequestAdapterStatus_Success) static_cast<WgpuContext*>(self)->adapter_ = a;
        *static_cast<bool*>(done) = true;
    };
    acb.userdata1 = ctx.get();
    acb.userdata2 = &adapter_done;
    wgpuInstanceRequestAdapter(ctx->instance_, nullptr, acb);
    process_until(ctx->instance_, [&] { return adapter_done; });
    if (!ctx->adapter_) return nullptr;

    bool device_done = false;
    WGPURequestDeviceCallbackInfo dcb{};
    dcb.mode = WGPUCallbackMode_AllowProcessEvents;
    dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView, void* self, void* done) {
        if (status == WGPURequestDeviceStatus_Success) static_cast<WgpuContext*>(self)->device_ = d;
        *static_cast<bool*>(done) = true;
    };
    dcb.userdata1 = ctx.get();
    dcb.userdata2 = &device_done;
    wgpuAdapterRequestDevice(ctx->adapter_, nullptr, dcb);
    process_until(ctx->instance_, [&] { return device_done; });
    if (!ctx->device_) return nullptr;
    ctx->queue_ = wgpuDeviceGetQueue(ctx->device_);
    return ctx;
}

WgpuContext::~WgpuContext() {
    if (queue_) wgpuQueueRelease(queue_);
    if (device_) wgpuDeviceRelease(device_);
    if (adapter_) wgpuAdapterRelease(adapter_);
    if (instance_) wgpuInstanceRelease(instance_);
}

void WgpuContext::wait_idle() const { wgpuDevicePoll(device_, true, nullptr); }

// ---- Renderer ----

WgpuRenderer::WgpuRenderer(const WgpuContext& ctx, WGPUTextureFormat target_format) : ctx_(ctx) {
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = sv(shader);
    WGPUShaderModuleDescriptor smd{};
    smd.nextInChain = &src.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(ctx.device(), &smd);

    WGPUVertexAttribute attrs[2]{};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = offsetof(render::ColorVertex, x);
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[1].offset = offsetof(render::ColorVertex, r);
    attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout layout{};
    layout.arrayStride = sizeof(render::ColorVertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = 2;
    layout.attributes = attrs;

    // Straight alpha: colour = src * a + dst * (1 - a).
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState target{};
    target.format = target_format;
    target.blend = &blend;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = sv("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor rpd{};
    rpd.vertex.module = module;
    rpd.vertex.entryPoint = sv("vs");
    rpd.vertex.bufferCount = 1;
    rpd.vertex.buffers = &layout;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.multisample.count = 1;
    rpd.multisample.mask = ~0u;
    rpd.fragment = &fragment;
    pipeline_ = wgpuDeviceCreateRenderPipeline(ctx.device(), &rpd);
    wgpuShaderModuleRelease(module);

    WGPUBufferDescriptor ubd{};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = 16;   // vec2f, padded to a uniform buffer's minimum binding size
    uniforms_ = wgpuDeviceCreateBuffer(ctx.device(), &ubd);
    WGPUBindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = uniforms_;
    entry.size = 16;
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline_, 0);
    bgd.entryCount = 1;
    bgd.entries = &entry;
    bind_group_ = wgpuDeviceCreateBindGroup(ctx.device(), &bgd);
    wgpuBindGroupLayoutRelease(bgd.layout);
}

WgpuRenderer::~WgpuRenderer() {
    if (vertices_) wgpuBufferRelease(vertices_);
    if (bind_group_) wgpuBindGroupRelease(bind_group_);
    if (uniforms_) wgpuBufferRelease(uniforms_);
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
}

void WgpuRenderer::render(const render::SceneSnapshot& snap, WGPUTextureView target, int width_px, int height_px,
                          float scale, core::Color clear) {
    auto tess = render::tessellate(snap);
    std::uint64_t bytes = tess.vertices.size() * sizeof(render::ColorVertex);
    if (bytes > vertex_capacity_) {   // grow by doubling: shapes change size frame to frame
        if (vertices_) wgpuBufferRelease(vertices_);
        vertex_capacity_ = std::max<std::uint64_t>(bytes, 2 * vertex_capacity_);
        WGPUBufferDescriptor vbd{};
        vbd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        vbd.size = vertex_capacity_;
        vertices_ = wgpuDeviceCreateBuffer(ctx_.device(), &vbd);
    }
    if (bytes > 0) wgpuQueueWriteBuffer(ctx_.queue(), vertices_, 0, tess.vertices.data(), bytes);
    float viewport[4] = {static_cast<float>(width_px) / scale, static_cast<float>(height_px) / scale, 0.f, 0.f};
    wgpuQueueWriteBuffer(ctx_.queue(), uniforms_, 0, viewport, sizeof viewport);

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(ctx_.device(), nullptr);
    WGPURenderPassColorAttachment ca{};
    ca.view = target;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {clear.r / 255.0, clear.g / 255.0, clear.b / 255.0, clear.a / 255.0};
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    if (bytes > 0) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group_, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices_, 0, bytes);
        for (const auto& b : tess.batches) {
            auto s = scissor_of(b.clip, width_px, height_px, scale);
            if (b.count == 0 || s.w == 0 || s.h == 0) continue;
            wgpuRenderPassEncoderSetScissorRect(pass, s.x, s.y, s.w, s.h);
            wgpuRenderPassEncoderDraw(pass, b.count, 1, b.first, 0);
        }
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(ctx_.queue(), 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
}

// ---- Offscreen ----

std::vector<std::uint8_t> render_to_pixels(const WgpuContext& ctx, const render::SceneSnapshot& snap,
                                           int width_px, int height_px, float scale, core::Color clear) {
    const auto w = static_cast<std::uint32_t>(width_px), h = static_cast<std::uint32_t>(height_px);
    WGPUTextureDescriptor td{};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {w, h, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(ctx.device(), &td);
    WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
    {
        WgpuRenderer renderer{ctx, td.format};
        renderer.render(snap, view, width_px, height_px, scale, clear);
    }

    const std::uint32_t row = (w * 4 + 255) / 256 * 256;   // copies need 256-byte rows
    WGPUBufferDescriptor bd{};
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = static_cast<std::uint64_t>(row) * h;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(ctx.device(), &bd);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(ctx.device(), nullptr);
    WGPUTexelCopyTextureInfo from{};
    from.texture = tex;
    WGPUTexelCopyBufferInfo to{};
    to.buffer = buf;
    to.layout.bytesPerRow = row;
    to.layout.rowsPerImage = h;
    WGPUExtent3D extent{w, h, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &from, &to, &extent);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(ctx.queue(), 1, &cmd);

    bool mapped = false;
    WGPUBufferMapCallbackInfo mcb{};
    mcb.mode = WGPUCallbackMode_AllowProcessEvents;
    mcb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ok, void*) {
        *static_cast<bool*>(ok) = status == WGPUMapAsyncStatus_Success;
    };
    mcb.userdata1 = &mapped;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, bd.size, mcb);
    for (int i = 0; i < 1000 && !mapped; ++i) {   // a software rasterizer can take a while
        ctx.wait_idle();
        wgpuInstanceProcessEvents(ctx.instance());
    }

    std::vector<std::uint8_t> pixels;
    if (mapped) {
        auto* src = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(buf, 0, bd.size));
        pixels.resize(static_cast<size_t>(w) * h * 4);
        for (std::uint32_t y = 0; y < h; ++y) std::memcpy(&pixels[y * w * 4], src + y * row, w * 4);
        wgpuBufferUnmap(buf);
    }
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(buf);
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(tex);
    return pixels;
}

} // namespace prism::backends
