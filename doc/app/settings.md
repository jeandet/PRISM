# Settings that persist

A struct of fields can also be the app's settings file. PRISM loads it when the app starts and
saves it shortly after each change. The user finds the app as they left it.

## The smallest example

```cpp
#include <prism/prism.hpp>
#include <prism/app/settings.hpp>   // the file code: only an app with settings pays for it
using namespace prism;

enum class Units { Metric, Imperial };

struct Settings {
    core::Field<ui::Slider<>> volume{{.value = 0.6}};
    core::Field<ui::Checkbox> autosave{{.checked = true, .label = "Save as I type"}};
    core::Field<Units> units{Units::Metric};
    app::WindowGeometry window;   // the window reopens at its last size and place
};

struct MyApp {
    Settings settings;
    void view(app::ViewBuilder& vb) {
        vb.vstack(settings.volume, settings.autosave, settings.units);
    }
};

int main() {
    MyApp app;
    return app::run(app, {.title = "My App", .settings = &app.settings,
                          .organization = "my-lab"});
}
```

Move the slider and quit. The next start shows the slider where you left it. The file looks like
this:

```toml
volume = 0.6
autosave = true
units = "Metric"

[window]
width = 800
height = 600
# then x, y, positioned and maximized
```

## What is saved, and how

Each member of the struct is one key.

| Member | Written as |
|---|---|
| `Field<int>`, `Field<double>`, `Field<bool>`, `Field<std::string>` | The value. |
| A widget field: `Field<ui::Slider<>>`, `Field<ui::TextField<>>`, `Field<ui::SpinBox<int>>`... | Only its value: the slider's position, the text. Its range and label stay in the code. |
| `Field<ui::Checkbox>` | Whether it is ticked. |
| `Field<SomeEnum>` (a scoped enum) | The value's name, `"Metric"`. |
| A plain struct member, such as a dock layout | Written whole. |
| A nested struct of fields | A `[table]` of its own. |

Saving waits for a short pause (300 ms), so a slider being dragged is written once, not a hundred
times. The write happens on a thread of its own and goes through a temporary file, so a crash
never leaves half a file. Quitting writes the last change.

## Where the file goes

The file is `settings.toml`, in a folder named after the organization and the app:

| System | Folder |
|---|---|
| Linux | `~/.local/share/<organization>/<name>/` |
| Windows | `%APPDATA%\<organization>\<name>\` |
| macOS | `~/Library/Application Support/<organization>/<name>/` |
| Browser | The page's IndexedDB storage. |

`<name>` is `AppConfig::name`, or the title in lower case with dashes: `"My App"` gives
`my-app`. `backends::settings_dir(organization, name)` gives the folder, for files of your own.

## The window's size and place

A member `app::WindowGeometry window` (that exact name) makes the main window reopen as it was
left:

| Member | Meaning |
|---|---|
| `width`, `height` | The last size. 0 means nothing is remembered yet. |
| `x`, `y`, `positioned` | The last place, and whether it is known. |
| `maximized` | Whether it was maximized. |

:::{note}
Wayland does not let an app place its window. There, only the size and the maximized state come
back.
:::

## Split panes

A splitter's pane sizes are settings like any other. Keep a `core::Field<ui::SplitSizes>` in the
struct and call `vb.remember_split(field)` inside the split container. See
[Layout](../widgets/layout.md).

## Without app::run

`app::SettingsFile` does the loading and saving on its own. It loads in its constructor and
writes the last change in its destructor:

```cpp
Settings s;
app::SettingsFile file{s, "/path/to/settings.toml"};
if (!file.load_error().empty()) std::println("{}", file.load_error());
```

| Call | What it does |
|---|---|
| `SettingsFile(S& settings, path file, milliseconds pause = 300ms)` | Loads `file` into `settings`, then saves after each change, `pause` after the last one. |
| `load_error()` | Why the file did not load. Empty when it did. |
| `to_toml(settings)` | The settings as TOML text. |
| `from_toml(settings, text)` | Sets every field the text names. Returns a `std::expected<void, std::string>`. |
| `load_settings(settings, path)` | Reads a file once. A missing file is not an error. |
| `save_settings(settings, path)` | Writes a file once. |

## Old and broken files

- A key the struct does not have is skipped. A file from an older or newer version still loads.
- An enum name the code does not know is skipped: that field keeps its default.
- A file that does not parse is renamed `settings.toml.broken`, and the app starts with the
  defaults. `load_error()` says why.

## Pitfalls

- Only `Field` members trigger a save. A plain `int` member is written, but changing it does not
  save the file.
- A `Field<ui::Password<>>` does not compile in a settings struct: a password does not belong
  in a plain file.
- The struct must not sit in an anonymous namespace. The TOML code reads member names, and
  clang cannot see them there.
- Settings are read and changed on the logic thread, like every field.
