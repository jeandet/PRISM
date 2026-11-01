// An SDL window drawn with WebGPU: a surface made from the window's native handles, configured
// to its size in pixels, and the WebGPU renderer drawing each snapshot into it.
#include "sdl_webgpu.hpp"

#include <prism/backends/wgpu_renderer.hpp>
#include <prism/ui/window_chrome.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace prism::backends {

struct WebGpuTarget {
    WgpuContext* ctx = nullptr;
    SDL_Window* window = nullptr;
    WGPUSurface surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUCompositeAlphaMode alpha = WGPUCompositeAlphaMode_Auto;
    WGPUPresentMode present_mode = WGPUPresentMode_Fifo;
    int width = 0, height = 0;   // as configured, in pixels
    std::unique_ptr<WgpuRenderer> renderer;
    int frames = 0;
#ifdef __APPLE__
    SDL_MetalView metal_view = nullptr;
#endif
};

namespace {

// The window's native surface, for the video driver SDL is using.
WGPUSurface make_surface(WGPUInstance instance, SDL_Window* window, [[maybe_unused]] WebGpuTarget& t) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    WGPUSurfaceDescriptor desc{};
#if defined(__APPLE__)
    t.metal_view = SDL_Metal_CreateView(window);
    if (!t.metal_view) return nullptr;
    WGPUSurfaceSourceMetalLayer metal{};
    metal.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metal.layer = SDL_Metal_GetLayer(t.metal_view);
    desc.nextInChain = &metal.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#elif defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND hwnd{};
    hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    hwnd.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    hwnd.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    desc.nextInChain = &hwnd.chain;
    return wgpuInstanceCreateSurface(instance, &desc);
#else
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "x11") == 0) {
        WGPUSurfaceSourceXlibWindow x11{};
        x11.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        x11.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        x11.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        desc.nextInChain = &x11.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    if (driver && std::strcmp(driver, "wayland") == 0) {
        WGPUSurfaceSourceWaylandSurface wl{};
        wl.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        wl.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        wl.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        desc.nextInChain = &wl.chain;
        return wgpuInstanceCreateSurface(instance, &desc);
    }
    return nullptr;   // e.g. the offscreen driver: no native surface
#endif
}

// simplify: the context picks its adapter before any window exists, so on a machine whose
// default GPU cannot present to this display (e.g. a discrete GPU under Xvfb, which lacks
// DRI3) the surface reports no formats and the window falls back to SDL_Renderer. Upgrade:
// request the adapter with `compatibleSurface` set to the first window's surface.

// The surface's preferred format, avoiding sRGB ones: PRISM's colours are sRGB values written as
// is, as SDL_Renderer does; an sRGB target would encode them a second time.
bool pick_format(WebGpuTarget& t) {
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(t.surface, t.ctx->adapter(), &caps) != WGPUStatus_Success || caps.formatCount == 0)
        return false;
    auto offers = [](auto* list, size_t n, auto value) { return std::find(list, list + n, value) != list + n; };
    t.format = caps.formats[0];
    for (auto f : {WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm})
        if (offers(caps.formats, caps.formatCount, f)) { t.format = f; break; }
    if (t.format == WGPUTextureFormat_BGRA8UnormSrgb || t.format == WGPUTextureFormat_RGBA8UnormSrgb)
        std::fprintf(stderr, "[prism] webgpu: only an sRGB surface format is offered; colours will look washed out\n");
    // PRISM writes straight alpha: an opaque window, else let the platform decide. Taking the
    // first mode could pick Premultiplied, which puts halos around anti-aliased text.
    t.alpha = WGPUCompositeAlphaMode_Auto;
    for (auto a : {WGPUCompositeAlphaMode_Opaque, WGPUCompositeAlphaMode_Auto})
        if (offers(caps.alphaModes, caps.alphaModeCount, a)) { t.alpha = a; break; }
    // Mailbox shows the newest frame at the next vblank without blocking: no tearing, and the
    // backend's frame pacer stays the only clock. Fifo (always offered) blocks in present until
    // a vblank, on top of the pacer, as SDL_Renderer's vsync does.
    // simplify: vsync cannot be turned off on this path (RenderConfig has no switch for it yet).
    t.present_mode = offers(caps.presentModes, caps.presentModeCount, WGPUPresentMode_Mailbox)
                   ? WGPUPresentMode_Mailbox : WGPUPresentMode_Fifo;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    return true;
}

void configure(WebGpuTarget& t, int width, int height) {
    WGPUSurfaceConfiguration cfg{};
    cfg.device = t.ctx->device();
    cfg.format = t.format;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.width = static_cast<std::uint32_t>(width);
    cfg.height = static_cast<std::uint32_t>(height);
    cfg.alphaMode = t.alpha;
    cfg.presentMode = t.present_mode;
    wgpuSurfaceConfigure(t.surface, &cfg);
    t.width = width;
    t.height = height;
}

// A lost surface cannot be reconfigured: make a new one for the same window. The renderer is
// rebuilt if the new surface wants another format (its pipeline is built for one).
bool recreate_surface(WebGpuTarget& t) {
    if (t.surface) wgpuSurfaceRelease(t.surface);
#ifdef __APPLE__
    if (t.metal_view) SDL_Metal_DestroyView(t.metal_view);
    t.metal_view = nullptr;
#endif
    t.width = t.height = 0;
    auto format = t.format;
    t.surface = make_surface(t.ctx->instance(), t.window, t);
    if (!t.surface || !pick_format(t)) {
        std::fprintf(stderr, "[prism] webgpu: the window's surface was lost and could not be recreated\n");
        return false;
    }
    if (t.format != format) t.renderer.reset();
    return true;
}

} // namespace

WebGpuTarget* make_webgpu_target(WgpuContext& ctx, SDL_Window* window) {
    auto t = std::make_unique<WebGpuTarget>();
    t->ctx = &ctx;
    t->window = window;
    t->surface = make_surface(ctx.instance(), window, *t);
    if (!t->surface || !pick_format(*t)) {
        std::fprintf(stderr, "[prism] webgpu: no surface for this window (video driver '%s')\n",
                     SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
        destroy_webgpu_target(t.release());
        return nullptr;
    }
    return t.release();
}

void destroy_webgpu_target(WebGpuTarget* t) {
    if (!t) return;
    t->renderer.reset();
    if (t->surface) {
        if (t->width > 0) wgpuSurfaceUnconfigure(t->surface);
        wgpuSurfaceRelease(t->surface);
    }
#ifdef __APPLE__
    if (t->metal_view) SDL_Metal_DestroyView(t->metal_view);
#endif
    delete t;
}

bool render_webgpu(WebGpuTarget& t, const render::SceneSnapshot& snap, SdlFontSet& fonts, const ui::Theme& theme,
                   app::DecorationMode decoration, const std::string& title, float scale, bool present) {
    // A surface frame that is acquired must be presented; nothing to draw into otherwise.
    if (!present || !t.surface) return false;
    if (!t.renderer) {
        SdlFontSet* f = &fonts;   // the backend's, alive as long as its windows
        t.renderer = std::make_unique<WgpuRenderer>(*t.ctx, t.format,
            [f](std::string_view text, float px, render::FontRole role) { return f->rasterize(text, px, role); });
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(t.window, &w, &h);
    if (w <= 0 || h <= 0) return false;   // minimised
    if (w != t.width || h != t.height) configure(t, w, h);

    WGPUSurfaceTexture frame{};
    wgpuSurfaceGetCurrentTexture(t.surface, &frame);
    bool ok = frame.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
           || frame.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    if (!ok) {
        if (frame.texture) wgpuTextureRelease(frame.texture);
        if (frame.status == WGPUSurfaceGetCurrentTextureStatus_Outdated) configure(t, w, h);
        else if (frame.status == WGPUSurfaceGetCurrentTextureStatus_Lost && recreate_surface(t)) configure(t, w, h);
        return false;
    }
    WGPUTextureView view = wgpuTextureCreateView(frame.texture, nullptr);

    // As the SDL path: content in the band below a custom title bar, then the bar on top.
    bool custom = decoration == app::DecorationMode::Custom;
    render::DrawList chrome;
    if (custom) {
        int pw = 0, ph = 0;
        SDL_GetWindowSize(t.window, &pw, &ph);
        ui::WindowChrome::render(chrome, pw, title, theme);
    }
    Layer layers[2] = {{&snap, nullptr, custom ? ui::WindowChrome::title_bar_h.raw() : 0.f},
                       {nullptr, &chrome, 0.f}};
    t.renderer->render(std::span{layers, custom ? 2u : 1u}, view, w, h, scale, theme.canvas_bg);

    wgpuTextureViewRelease(view);
    wgpuSurfacePresent(t.surface);
    ++t.frames;
    wgpuTextureRelease(frame.texture);
    return true;
}

int webgpu_frames(const WebGpuTarget& t) { return t.frames; }

WgpuContext* create_webgpu_context() {
    auto ctx = WgpuContext::create();
    if (!ctx) std::fprintf(stderr, "[prism] warning: WebGPU requested, but no adapter was found; "
                                   "drawing with SDL_Renderer\n");
    return ctx.release();
}
void destroy_webgpu_context(WgpuContext* ctx) { delete ctx; }

} // namespace prism::backends
