// Builds without -Dwebgpu=enabled: windows always draw with SDL_Renderer.
#include "sdl_webgpu.hpp"

#include <cstdio>

namespace prism::backends {

WebGpuTarget* make_webgpu_target(WgpuContext&, SDL_Window*) { return nullptr; }
void destroy_webgpu_target(WebGpuTarget*) {}
bool render_webgpu(WebGpuTarget&, const render::SceneSnapshot&, SdlFontSet&, const ui::Theme&, app::DecorationMode,
                   const std::string&, float, bool) { return false; }
int webgpu_frames(const WebGpuTarget&) { return 0; }

WgpuContext* create_webgpu_context() {
    std::fprintf(stderr, "[prism] warning: WebGPU requested, but this build has none "
                         "(-Dwebgpu=enabled); drawing with SDL_Renderer\n");
    return nullptr;
}
void destroy_webgpu_context(WgpuContext*) {}

} // namespace prism::backends
