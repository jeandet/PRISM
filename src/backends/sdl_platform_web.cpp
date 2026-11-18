#include "sdl_platform.hpp"

#include <emscripten.h>

namespace prism::backends {

namespace {

// A trackpad pinch reaches the page as a wheel event with ctrlKey set and no Control key
// pressed, and SDL's wheel handler drops ctrlKey: this listener keeps the last wheel event's
// Ctrl (1) and Meta (2) for the backend. It runs before SDL's (capture, on the window), and
// keeps the browser from zooming the page on a Ctrl+wheel over a canvas.
// simplify: the keys of the last wheel event seen, not of each one: wheel events queued between
// two polls with different keys all get the last one's. Fine for a pinch; carry the keys per
// event (patch SDL's handler) if that ever shows.
EM_JS(void, prism_watch_wheel_keys, (), {
    if (typeof window === 'undefined') return;
    globalThis.prismWheelKeys = 0;
    window.addEventListener('wheel', (e) => {
        globalThis.prismWheelKeys = (e.ctrlKey ? 1 : 0) | (e.metaKey ? 2 : 0);
        if (e.ctrlKey && e.target instanceof HTMLCanvasElement) e.preventDefault();
    }, {capture: true, passive: false});
});

EM_JS(int, prism_wheel_keys, (), { return globalThis.prismWheelKeys || 0; });

// Installed before main, so the first pinch is seen too.
[[maybe_unused]] const bool watching_wheel_keys = (prism_watch_wheel_keys(), true);

} // namespace

// In a browser the page is the window: no title bar or window buttons (the browser has
// its own), and the canvas fills the page and follows it when the browser window resizes.
app::WindowConfig platform_window_config(app::WindowConfig requested) {
    requested.decoration = app::DecorationMode::None;
    return requested;
}

SDL_WindowFlags platform_window_flags() { return SDL_WINDOW_FILL_DOCUMENT; }

bool platform_opens_windows() { return false; }

SDL_Keymod platform_wheel_mods() {
    const int keys = prism_wheel_keys();
    return static_cast<SDL_Keymod>(SDL_GetModState() | ((keys & 1) ? SDL_KMOD_LCTRL : 0) | ((keys & 2) ? SDL_KMOD_LGUI : 0));
}

} // namespace prism::backends
