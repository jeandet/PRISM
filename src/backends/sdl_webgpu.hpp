#pragma once

// An SDL window drawn with WebGPU instead of SDL_Renderer (internal to the SDL backend).
// Implemented in sdl_webgpu.cpp with -Dwebgpu=enabled, by stubs in sdl_webgpu_stub.cpp otherwise.

#include <prism/app/window.hpp>
#include <prism/backends/sdl_font_set.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/ui/context.hpp>

#include <SDL3/SDL.h>

#include <string>

namespace prism::backends {

class WgpuContext;
struct WebGpuTarget;

// nullptr when this build has no WebGPU or the window's surface cannot be made.
WebGpuTarget* make_webgpu_target(WgpuContext& ctx, SDL_Window* window);
void destroy_webgpu_target(WebGpuTarget* target);

// Draws one frame: content below the title bar when the decoration is Custom, the bar on top.
// false when the surface was not ready (resized, hidden or lost): it is reconfigured or remade,
// try next frame. present = false draws nothing: a surface frame, once taken, must be presented.
bool render_webgpu(WebGpuTarget& target, const render::SceneSnapshot& snap, SdlFontSet& fonts,
                   const ui::Theme& theme, app::DecorationMode decoration, const std::string& title,
                   float scale, bool present);

int webgpu_frames(const WebGpuTarget& target);   // frames presented

// The backend's shared context; nullptr (with a warning) when there is no WebGPU or no adapter.
WgpuContext* create_webgpu_context();
void destroy_webgpu_context(WgpuContext* ctx);

} // namespace prism::backends
