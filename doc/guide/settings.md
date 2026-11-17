# Settings that persist

A struct of fields is also a settings file. Hand it to `app::run` as `.settings`: it is loaded
at start and saved shortly after each change, as TOML in the user's settings folder
(`<config dir>/<organization>/<name>/settings.toml`). A widget field keeps only its value, an
enum is written by name, and a nested struct becomes a `[table]`:

```cpp
#include <prism/app/settings.hpp>   // the TOML code: only an app with settings pays for it

struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
    core::Field<Theme> theme{Theme::System};
    Editor editor;              // [editor] font_size = 12 ...
    app::WindowGeometry window; // optional: the window reopens at its last size and place
};

int main() {
    MyApp app;                  // holds a Settings named `settings`
    return app::run(app, {.title = "My app", .width = 1000, .height = 640,
                          .settings = &app.settings, .organization = "my-org"},
                    [&](app::AppContext& ctx) { /* setup, on the logic thread */ });
}
```

```toml
volume = 0.6
theme = "System"

[editor]
font_size = 12
```

The window's size, place and maximized state are remembered when the struct has an
`app::WindowGeometry window` (Wayland does not let an app place its window: there, only size and
maximized). A splitter's pane sizes are settings like any other: a `core::Field<ui::SplitSizes>`
in the struct, and `vb.remember_split(settings.sidebar)` inside the split container.

Without `app::run`, `app::SettingsFile settings{s, path}` does the loading and saving on its own.

Unknown keys are skipped, so a file from another version still loads. It is built on
[Glaze](https://github.com/stephenberry/glaze), which also works without `-freflection`.
`app::to_toml`, `from_toml`, `load_settings` and `save_settings` are there for one-off use.
