# Windows

Every PRISM app starts with one window. This page shows how to open it, how to size and decorate
it, and how to open more windows while the app runs.

## The main window

`app::run` opens the main window, shows your model in it, and returns when the user closes it:

```cpp
#include <prism/prism.hpp>
using namespace prism;

struct Hello {
    core::Field<std::string> greeting{"Hello, ada"};
};

int main() {
    Hello model;
    return app::run(model, {.title = "Hello", .width = 400, .height = 120});
}
```

The second argument is an `app::AppConfig`. Every member has a default, so name only the ones
you need:

| Member | Default | Meaning |
|---|---|---|
| `title` | `"PRISM"` | The window's title. |
| `width`, `height` | `800`, `600` | The window's size, in pixels. |
| `decoration` | `DecorationMode::Custom` | Who draws the title bar (see below). |
| `settings` | none | A struct of fields saved between runs ([Settings](settings.md)). |
| `organization` | empty | The settings folder's parent. |
| `name` | the title, dashed | The settings folder's own name (`"My App"` gives `my-app`). |

`app::run` takes an optional third argument: a function run once on the logic thread, before
the first frame. It receives the app's `AppContext`, which opens windows, changes the theme and
more ([The app context](app-context.md)):

```cpp
app::run(model, {.title = "Hello"}, [](app::AppContext& ctx) {
    ctx.follow_system_theme();
});
```

`app::run` always returns 0, so `return app::run(...)` ends `main` cleanly.

## Title bar and borders

`decoration` picks who draws the window's frame:

| `app::DecorationMode` | What you get |
|---|---|
| `Custom` (default) | PRISM draws the title bar, its buttons and the resize edges. It looks the same on every system. |
| `Native` | The system's window manager draws them. |
| `None` | No frame at all: a borderless window. |

```cpp
app::run(model, {.title = "Kiosk", .width = 1024, .height = 768,
                 .decoration = app::DecorationMode::None});
```

:::{warning}
On Wayland, many desktops draw no frame for an app. There, `Native` gives a window with no
title bar and no close button. That is why `Custom` is the default.
:::

## More windows

A second window shows a model of its own. Open it from the logic thread, with
`ctx.open_window(model, config)`:

```cpp
struct Editor {
    core::Field<ui::TextField<>> text{{.value = "Some text"}};
    void view(app::ViewBuilder& vb) { vb.widget(text); }
};

struct Stats {
    Editor* editor;
    core::Derived<std::string> summary{[this] {
        return std::to_string(editor->text.get().value.size()) + " characters";
    }};
    void view(app::ViewBuilder& vb) { vb.widget(summary); }
};

int main() {
    Editor editor;
    Stats stats{&editor};
    return app::run(editor, {.title = "Editor"}, [&](app::AppContext& ctx) {
        ctx.open_window(stats, {.title = "Statistics", .width = 320, .height = 80});
    });
}
```

The second model reads the first one's field, so its summary follows every edit. Both windows
share the logic thread, so they never see each other half-changed.

The second argument is an `app::WindowConfig`:

| Member | Default | Meaning |
|---|---|---|
| `title` | `"PRISM"` | The title, as a C string. |
| `width`, `height` | `800`, `600` | The size, in pixels. |
| `resizable` | `true` | Whether the user may resize it. |
| `fullscreen` | `false` | Open in full screen. |
| `decoration` | `Custom` | As above. |
| `x`, `y` | none | Where on the desktop. None lets the system choose. Wayland always chooses. |
| `maximized` | `false` | Open maximized. |

The window's lifetime:

| Call | What it does |
|---|---|
| `WindowId ctx.open_window(Model&, WindowConfig)` | Opens the window. Returns its id, or 0 if no window could open. |
| `bool ctx.window_open(WindowId)` | Whether it is still open. |
| `void ctx.close_window(WindowId)` | Closes it from code. Not the main window: closing that ends the app. |

When the user closes a second window, it goes and the app goes on. The model must outlive the
window: keep it in `main` or in the main model.

:::{note}
In the browser there is one canvas, so `open_window` returns 0 there. A dock's floating panel
becomes a card over the dock instead ([Docking](../widgets/docking.md)).
:::

## Choosing the renderer

By default PRISM draws with SDL's renderer. A build with `-Dwebgpu=enabled` adds an experimental
WebGPU renderer. The environment variable `PRISM_RENDERER` picks one for any app, with no code
change:

```bash
PRISM_RENDERER=webgpu ./builddir/examples/model_notes/model_notes
```

In code, make the backend yourself and hand it to `app::model_app`, the call `app::run` is built
on:

```cpp
auto backend = app::Backend::software({.renderer = app::Renderer::WebGpu});
auto& window = backend.create_window({.title = "Notes", .width = 900, .height = 600});
app::model_app(backend, window, model);
```

`app::RenderConfig` also sets the fonts and the frame rate:

| Member | Default | Meaning |
|---|---|---|
| `renderer` | `Renderer::Sdl` | `Sdl` or `WebGpu`. A window that cannot get a WebGPU surface falls back to `Sdl`, with a warning. |
| `font_path` | bundled Noto Sans | The UI font file. |
| `mono_font_path` | bundled JetBrains Mono | The font for code and text areas. |
| `target_fps` | `0` | 0 follows the display's refresh rate. A number caps the frames per second. |
| `frame_pacing` | `true` | Present on frame boundaries, not after every batch of events. |

## Pitfalls

- `open_window` and `close_window` belong to the logic thread. From a worker, hand the call over
  with `ctx.poster()` ([The app context](app-context.md)).
- The model shown in a window must outlive it. A model built on the stack of a callback is gone
  when the callback returns.
- `WindowConfig::title` is a C string: it must stay valid until the window is open.
