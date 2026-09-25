#include "sdl_platform.hpp"

namespace prism::backends {

// In a browser the page is the window: no title bar or window buttons (the browser has
// its own), and the canvas fills the page and follows it when the browser window resizes.
app::WindowConfig platform_window_config(app::WindowConfig requested) {
    requested.decoration = app::DecorationMode::None;
    return requested;
}

SDL_WindowFlags platform_window_flags() { return SDL_WINDOW_FILL_DOCUMENT; }

} // namespace prism::backends
