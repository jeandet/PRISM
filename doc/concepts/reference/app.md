# app::run and AppContext

`#include <prism/prism.hpp>` · namespace `prism::app`

`app::run` opens a window, shows a model in it, and runs until the window closes.
`AppContext` is what the running app offers your code: delivering worker values, posting work,
the clock, windows, themes, file dialogs.

## app::run

```cpp
#include <prism/app/settings.hpp>   // needed to pass .settings

struct Settings {
    core::Field<bool> dark{false};
    app::WindowGeometry window;   // the window's size and place, remembered
};

struct Notes {   // the app's model
    core::Field<ui::TextArea<>> text{};
};

int main() {
    Notes notes;
    Settings settings;
    return app::run(notes, {.title = "Notes", .width = 900, .height = 600,
                            .settings = &settings, .organization = "acme"},
                    [&](app::AppContext& ctx) {
                        ctx.follow_system_theme();
                    });
}
```

| Signature | Meaning |
|---|---|
| `int run(Model& model, const AppConfig& cfg, std::function<void(AppContext&)> setup = {})` | opens the window, runs `setup` once on the logic thread, shows `model` until the window closes; returns 0 |

The model must outlive the call. While `run` runs, the model belongs to the logic thread
([Threads](../threads.md)); after it returns, it is the caller's again.

`setup` runs after every block's `attach`, before the first frame.

### AppConfig

| Member | Default | Meaning |
|---|---|---|
| `title` | `"PRISM"` | the window's title |
| `width`, `height` | 800, 600 | the window's size, in points |
| `settings` | none | `&my_settings`: a struct of fields saved when one changes and loaded at start (needs `#include <prism/app/settings.hpp>`) |
| `organization` | empty | the settings folder's parent |
| `name` | the title, dashed | the settings folder: `"PRISM Notes"` gives `prism-notes` |
| `decoration` | `DecorationMode::Custom` | who draws the title bar: `Custom` (PRISM, the same everywhere), `Native` (the OS), `None` (borderless) |

A settings struct with a `WindowGeometry window` member also remembers the window's size and
place. [Settings](../../app/settings.md) has the details.

### Other ways to start

| Entry point | When |
|---|---|
| `app::run(model, cfg, setup)` | almost always |
| `app::model_app(backend, window, model, setup)` | on a backend and window you made yourself, to pick a renderer or fonts |
| `app::model_app(WindowConfig{...}, model, setup)` | with a full `WindowConfig`: `resizable`, `fullscreen`, `maximized`, position |

## AppContext

The app context is how your code reaches the running app: threads, the clock, windows, the
theme, file dialogs. Your code gets it in a block's `attach(app::AppContext&)` and in the setup
function of `app::run`. Its full reference is on [The app context](../../app/app-context.md).
