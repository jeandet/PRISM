#include <prism/backends/wgpu_renderer.hpp>

#include <prism/render/tessellate.hpp>

#ifndef __EMSCRIPTEN__
#include <webgpu/wgpu.h>   // wgpuDevicePoll: wgpu-native's blocking wait (desktop only)
#endif

#include <algorithm>
#include <array>
#include <initializer_list>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace prism::backends {

namespace {

WGPUStringView sv(std::string_view s) { return WGPUStringView{s.data(), s.size()}; }

// Waits for an async request by processing instance events, for at most `limit`: a software
// rasterizer on a loaded CI machine can take seconds. false on time out.
// simplify: polls with a short sleep; wgpuInstanceWaitAny with a timeout needs the instance's
// TimedWaitAny feature -- switch to it once wgpu-native and Dawn both support it.
template <typename Done>
bool process_until(WGPUInstance inst, Done done, std::chrono::seconds limit = std::chrono::seconds{20}) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        wgpuInstanceProcessEvents(inst);
        if (!done()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}

constexpr std::string_view shader = R"(
struct Uniforms { viewport: vec2f };   // the target's size in points
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var tex: texture_2d<f32>;
@group(1) @binding(1) var samp: sampler;

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

@vertex fn vs(@location(0) pos: vec2f, @location(1) uv: vec2f, @location(2) color: vec4f) -> VertexOut {
    var out: VertexOut;
    out.position = vec4f(pos.x / u.viewport.x * 2.0 - 1.0, 1.0 - pos.y / u.viewport.y * 2.0, 0.0, 1.0);
    out.uv = uv;
    out.color = color;
    return out;
}

// Shapes sample a white texel; text is white with coverage in alpha; images carry their colour.
@fragment fn fs(in: VertexOut) -> @location(0) vec4f { return textureSample(tex, samp, in.uv) * in.color; }
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
    // Errors nobody captured (a bad draw, a lost device) go to stderr rather than vanish.
    WGPUDeviceDescriptor dd{};
    dd.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView msg, void*, void*) {
        std::fprintf(stderr, "prism webgpu: %.*s\n", static_cast<int>(msg.length), msg.data);
    };
    dd.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    dd.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView msg, void*, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
            std::fprintf(stderr, "prism webgpu: device lost: %.*s\n", static_cast<int>(msg.length), msg.data);
    };
    wgpuAdapterRequestDevice(ctx->adapter_, &dd, dcb);
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

void WgpuContext::wait_idle() const {
#ifndef __EMSCRIPTEN__
    wgpuDevicePoll(device_, true, nullptr);
#endif
}

// ---- Renderer ----

namespace {

WGPUBindGroupLayoutEntry layout_entry(std::uint32_t binding, WGPUShaderStage visibility) {
    WGPUBindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = visibility;
    return e;
}

WGPUBindGroupLayout make_layout(WGPUDevice device, std::initializer_list<WGPUBindGroupLayoutEntry> entries) {
    std::vector<WGPUBindGroupLayoutEntry> v(entries);
    WGPUBindGroupLayoutDescriptor d{};
    d.entryCount = v.size();
    d.entries = v.data();
    return wgpuDeviceCreateBindGroupLayout(device, &d);
}

void release(WgpuRenderer::Texture& t) {
    if (t.group) wgpuBindGroupRelease(t.group);
    if (t.view) wgpuTextureViewRelease(t.view);
    if (t.texture) wgpuTextureRelease(t.texture);
    t = {};
}

// Six vertices of a textured quad (corners top-left, top-right, bottom-right, bottom-left).
void push_quad(std::vector<render::Vertex>& out, const std::array<render::Vec2, 4>& q, core::Color c) {
    constexpr float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f, a = c.a / 255.f;
    for (int i : {0, 1, 2, 0, 2, 3}) out.push_back({q[i].x, q[i].y, uv[i][0], uv[i][1], r, g, b, a});
}

} // namespace

WgpuRenderer::WgpuRenderer(const WgpuContext& ctx, WGPUTextureFormat target_format, TextRasterizer text)
    : ctx_(ctx), rasterize_(std::move(text)) {
    // WebGPU reports a rejected shader or pipeline asynchronously, with an invalid (not null)
    // object: catch it in an error scope and throw with the reason.
    wgpuDevicePushErrorScope(ctx.device(), WGPUErrorFilter_Validation);
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = sv(shader);
    WGPUShaderModuleDescriptor smd{};
    smd.nextInChain = &src.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(ctx.device(), &smd);

    auto uniform_entry = layout_entry(0, WGPUShaderStage_Vertex);
    uniform_entry.buffer.type = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayout uniform_layout = make_layout(ctx.device(), {uniform_entry});
    auto tex_entry = layout_entry(0, WGPUShaderStage_Fragment);
    tex_entry.texture.sampleType = WGPUTextureSampleType_Float;
    tex_entry.texture.viewDimension = WGPUTextureViewDimension_2D;
    auto sampler_entry = layout_entry(1, WGPUShaderStage_Fragment);
    sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;
    texture_layout_ = make_layout(ctx.device(), {tex_entry, sampler_entry});
    WGPUBindGroupLayout groups[2] = {uniform_layout, texture_layout_};
    WGPUPipelineLayoutDescriptor pld{};
    pld.bindGroupLayoutCount = 2;
    pld.bindGroupLayouts = groups;
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pld);

    WGPUVertexAttribute attrs[3]{};
    attrs[0] = {nullptr, WGPUVertexFormat_Float32x2, offsetof(render::Vertex, x), 0};
    attrs[1] = {nullptr, WGPUVertexFormat_Float32x2, offsetof(render::Vertex, u), 1};
    attrs[2] = {nullptr, WGPUVertexFormat_Float32x4, offsetof(render::Vertex, r), 2};
    WGPUVertexBufferLayout layout{};
    layout.arrayStride = sizeof(render::Vertex);
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = 3;
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
    rpd.layout = pipeline_layout;
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
    wgpuPipelineLayoutRelease(pipeline_layout);

    WGPUBufferDescriptor ubd{};
    ubd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    ubd.size = 16;   // vec2f, padded to a uniform buffer's minimum binding size
    uniforms_ = wgpuDeviceCreateBuffer(ctx.device(), &ubd);
    WGPUBindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = uniforms_;
    entry.size = 16;
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = uniform_layout;
    bgd.entryCount = 1;
    bgd.entries = &entry;
    uniform_group_ = wgpuDeviceCreateBindGroup(ctx.device(), &bgd);
    wgpuBindGroupLayoutRelease(uniform_layout);

    WGPUSamplerDescriptor sd{};
    sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.lodMinClamp = 0.f;
    sd.lodMaxClamp = 32.f;
    sd.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(ctx.device(), &sd);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    white_ = upload(1, 1, white);

    struct Verdict { bool done = false; std::string error; } verdict;
    WGPUPopErrorScopeCallbackInfo pcb{};
    pcb.mode = WGPUCallbackMode_AllowProcessEvents;
    pcb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type, WGPUStringView msg, void* v, void*) {
        auto& out = *static_cast<Verdict*>(v);
        if (type != WGPUErrorType_NoError) out.error.assign(msg.data ? msg.data : "", msg.length);
        out.done = true;
    };
    pcb.userdata1 = &verdict;
    wgpuDevicePopErrorScope(ctx.device(), pcb);
    process_until(ctx.instance(), [&] { return verdict.done; });
    if (!verdict.error.empty()) throw std::runtime_error("prism webgpu: renderer setup failed: " + verdict.error);
}

WgpuRenderer::~WgpuRenderer() {
    text_cache_.clear([](Texture& t) { release(t); });
    image_cache_.clear([](Texture& t) { release(t); });
    release(white_);
    if (vertices_) wgpuBufferRelease(vertices_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (uniform_group_) wgpuBindGroupRelease(uniform_group_);
    if (uniforms_) wgpuBufferRelease(uniforms_);
    if (texture_layout_) wgpuBindGroupLayoutRelease(texture_layout_);
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
}

WgpuRenderer::Texture WgpuRenderer::upload(int width, int height, const std::uint8_t* rgba) {
    Texture t;
    t.width = width;
    t.height = height;
    WGPUTextureDescriptor td{};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    t.texture = wgpuDeviceCreateTexture(ctx_.device(), &td);
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = t.texture;
    WGPUTexelCopyBufferLayout src{};
    src.bytesPerRow = static_cast<std::uint32_t>(width) * 4;
    src.rowsPerImage = static_cast<std::uint32_t>(height);
    wgpuQueueWriteTexture(ctx_.queue(), &dst, rgba, static_cast<size_t>(width) * height * 4, &src, &td.size);
    t.view = wgpuTextureCreateView(t.texture, nullptr);
    WGPUBindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].textureView = t.view;
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    WGPUBindGroupDescriptor bgd{};
    bgd.layout = texture_layout_;
    bgd.entryCount = 2;
    bgd.entries = entries;
    t.group = wgpuDeviceCreateBindGroup(ctx_.device(), &bgd);
    return t;
}

// Rasterized at size x scale device pixels, like the SDL backend, keyed by role, size and text.
const WgpuRenderer::Texture* WgpuRenderer::text_texture(const render::TextCmd& cmd, float scale) {
    if (!rasterize_) return nullptr;
    float px_size = cmd.size * scale;
    key_.assign(1 + sizeof px_size, '\0');
    key_[0] = static_cast<char>(cmd.role);
    std::memcpy(key_.data() + 1, &px_size, sizeof px_size);
    key_ += cmd.text;
    if (auto* hit = text_cache_.find(key_)) return hit;
    auto bitmap = rasterize_(cmd.text, px_size, cmd.role);
    if (!bitmap || bitmap->width <= 0 || bitmap->height <= 0) return nullptr;
    return &text_cache_.insert(key_, upload(bitmap->width, bitmap->height, bitmap->rgba.data()));
}

// Uploaded on first use, keyed by the image's id (images are immutable).
const WgpuRenderer::Texture* WgpuRenderer::image_texture(const render::ImageCmd& cmd) {
    const auto& img = *cmd.image;
    std::uint64_t id = img.id();
    key_.assign(sizeof id, '\0');
    std::memcpy(key_.data(), &id, sizeof id);
    if (auto* hit = image_cache_.find(key_)) return hit;
    return &image_cache_.insert(key_, upload(img.width(), img.height(), img.pixels().data()));
}

void WgpuRenderer::render(const render::SceneSnapshot& snap, WGPUTextureView target, int width_px, int height_px,
                          float scale, core::Color clear) {
    struct Draw {
        std::optional<core::Rect> clip;
        std::uint32_t first, count;
        WGPUBindGroup group;
    };
    auto tess = render::tessellate(snap);
    std::vector<Draw> draws;
    for (const auto& b : tess.batches) {
        if (auto* text = std::get_if<const render::TextCmd*>(&b.source)) {
            const Texture* t = text_texture(**text, scale);
            if (!t) continue;
            auto first = static_cast<std::uint32_t>(tess.vertices.size());
            push_quad(tess.vertices, render::text_quad(**text, t->width / scale, t->height / scale), (*text)->color);
            draws.push_back({b.clip, first, 6, t->group});
        } else if (auto* image = std::get_if<const render::ImageCmd*>(&b.source)) {
            const Texture* t = image_texture(**image);
            const auto& r = (*image)->rect;
            float x = r.origin.x.raw(), y = r.origin.y.raw(), w = r.extent.w.raw(), h = r.extent.h.raw();
            auto first = static_cast<std::uint32_t>(tess.vertices.size());
            push_quad(tess.vertices, {{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}}, core::Color::rgba(255, 255, 255));
            draws.push_back({b.clip, first, 6, t->group});
        } else if (b.count > 0) {
            draws.push_back({b.clip, b.first, b.count, white_.group});
        }
    }
    text_cache_.end_frame(2, [](Texture& t) { release(t); });
    image_cache_.end_frame(2, [](Texture& t) { release(t); });

    std::uint64_t bytes = tess.vertices.size() * sizeof(render::Vertex);
    // Grows by doubling and never shrinks: shapes change size frame to frame, and one large
    // frame keeps its buffer for the session (bounded by the largest frame drawn).
    if (bytes > vertex_capacity_) {
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
        wgpuRenderPassEncoderSetBindGroup(pass, 0, uniform_group_, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertices_, 0, bytes);
        for (const auto& d : draws) {
            auto s = scissor_of(d.clip, width_px, height_px, scale);
            if (s.w == 0 || s.h == 0) continue;
            wgpuRenderPassEncoderSetScissorRect(pass, s.x, s.y, s.w, s.h);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, d.group, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, d.count, 1, d.first, 0);
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
                                           int width_px, int height_px, float scale, core::Color clear,
                                           TextRasterizer text) {
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
    {   // a renderer per call: fine for tests and one-off exports; windows keep theirs
        WgpuRenderer renderer{ctx, td.format, std::move(text)};
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
    ctx.wait_idle();
    process_until(ctx.instance(), [&] { return mapped; });

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
