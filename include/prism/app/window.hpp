#pragma once

#include <prism/input/input_event.hpp>
#include <prism/render/scene_snapshot.hpp>
#include <prism/ui/cursor.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace prism::app {
using namespace prism::input;
using namespace prism::ui;


using WindowId = uint32_t;

enum class DecorationMode { Native, Custom, None };

struct WindowConfig {
    const char* title      = "PRISM";
    int         width      = 800;
    int         height     = 600;
    bool        resizable  = true;
    bool        fullscreen = false;
    // Custom, not Native: every real window this project ships (all examples, the debug
    // inspector) already opts into Custom by hand, because native/server-side decoration is
    // unavailable in this project's own dev environment (Wayland gives none at all) -- Native
    // as a default has silently produced a chromeless, uncloseable window twice already.
    DecorationMode decoration = DecorationMode::Custom;
};

// How a backend draws. Sdl is SDL_Renderer; WebGpu needs a build with -Dwebgpu=enabled and a
// GPU (or Mesa's lavapipe), and falls back to Sdl with a warning otherwise. The environment
// variable PRISM_RENDERER=webgpu (or sdl) overrides the choice, to try it on any app.
enum class Renderer { Sdl, WebGpu };

struct RenderConfig {
    const char* font_path = nullptr;        // UI font (proportional); null = bundled Noto Sans
    const char* mono_font_path = nullptr;   // code/TextArea font; null = bundled JetBrains Mono
    // Present pacing (SoftwareBackend): presents happen on frame boundaries instead
    // of after every event batch. false = legacy event-rate-bound behavior.
    bool frame_pacing = true;
    // 0 = follow the display's refresh rate; >0 = fixed cap in frames per second.
    float target_fps = 0;
    Renderer renderer = Renderer::Sdl;
};

class Window {
public:
    virtual ~Window() = default;

    virtual WindowId id() const = 0;

    virtual void set_title(std::string_view title) = 0;
    virtual void set_size(int w, int h) = 0;
    virtual std::pair<int, int> size() const = 0;
    virtual void set_position(int x, int y) = 0;
    virtual std::pair<int, int> position() const = 0;

    virtual void set_decoration_mode(DecorationMode mode) = 0;
    virtual DecorationMode decoration_mode() const = 0;

    virtual void set_resizable(bool r) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_fullscreen(bool f) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;

    virtual void close() = 0;

    virtual void set_cursor(CursorShape shape) = 0;
};

struct WindowEvent {
    WindowId window;
    InputEvent event;
    // The frame on screen when the backend read the event. Input is hit-tested against it, so
    // a click lands on what the user saw even when a newer frame is published but not yet
    // shown. Null: the latest published frame.
    std::shared_ptr<const render::SceneSnapshot> shown = nullptr;
};

} // namespace prism::app
