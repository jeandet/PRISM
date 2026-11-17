#include "sdl_platform.hpp"

namespace prism::backends {

app::WindowConfig platform_window_config(app::WindowConfig requested) { return requested; }

SDL_WindowFlags platform_window_flags() { return 0; }

bool platform_opens_windows() { return true; }

} // namespace prism::backends
