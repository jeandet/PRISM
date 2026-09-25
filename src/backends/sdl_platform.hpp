#pragma once

#include <prism/app/window.hpp>

#include <SDL3/SDL.h>

namespace prism::backends {

// How the platform adapts the window an app asked for. The build links exactly one
// implementation: sdl_platform_desktop.cpp (identity) or sdl_platform_web.cpp (emscripten),
// so app code stays the same everywhere.
app::WindowConfig platform_window_config(app::WindowConfig requested);
SDL_WindowFlags platform_window_flags();

} // namespace prism::backends
