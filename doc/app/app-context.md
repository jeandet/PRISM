# The app context

`app::AppContext` is the running app's handle. Through it, code reaches what lies outside the
model: other threads, windows, the theme, the platform's dialogs. This page lists what an app
uses it for.

## Getting it

`app::run` hands it to the setup function, on the logic thread, before the first frame:

```cpp
app::run(model, {.title = "My App"}, [&](app::AppContext& ctx) {
    ctx.follow_system_theme();
});
```

A block gets it in its optional `attach` function, called once when the app starts:

```cpp
struct Clock {
    core::Field<std::string> now{"--:--"};
    void attach(app::AppContext& ctx) { /* keep &ctx, start a timer... */ }
    void view(app::ViewBuilder& vb) { vb.widget(now); }
};
```

The context lives as long as the app runs. Keeping a pointer to it in a model is fine.

## A complete example: a worker reports progress

```cpp
#include <prism/prism.hpp>
#include <thread>
using namespace prism;

struct Job {
    core::Field<ui::ProgressBar> progress{{.value = 0.0, .label = "Waiting"}};
    core::Shared<double> done{0.0};   // written by the worker

    void view(app::ViewBuilder& vb) { vb.widget(progress); }
};

int main() {
    Job job;
    std::jthread worker;
    return app::run(job, {.title = "Job", .width = 300, .height = 80}, [&](app::AppContext& ctx) {
        ctx.receive(job.done, [&](const double& d) {   // on the logic thread
            job.progress = {.value = d, .label = d < 1.0 ? "Working" : "Done"};
        });
        worker = std::jthread([&] {
            for (int i = 1; i <= 100; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                job.done = i / 100.0;   // any thread
            }
        });
    });
}
```

The worker never touches a widget. It sets a `Shared`, and `ctx.receive` brings each value to
the logic thread, where the field may change. See [Threads](../concepts/threads.md) for the
whole story.

## Reference

### Threads

| Call | What it does |
|---|---|
| `receive(source, fn)` | `source` is a `core::Shared<T>` or a `core::Channel<T>`. What another thread puts in it reaches `fn` on the logic thread. A `Shared` hands over the latest value; a `Channel` every value, in order. |
| `poster()` | A function any thread may call with work to run on the logic thread. It may outlive the app: after the app ends, it drops the work. |
| `waker()` | A function any thread may call to wake the logic thread for a publish. Same lifetime rule. |
| `post(fn)` | Runs `fn` on the logic thread, then publishes. Any thread: called on the logic thread it runs at once, elsewhere it is queued. Use `poster()` from a thread that may outlive the app. |
| `scheduler()` | The logic thread's `std::execution` scheduler, for sender pipelines. |

### The clock

| Call | What it does |
|---|---|
| `clock().add(fn)` | Calls `fn(now)` again and again on the logic thread while it is added. `fn` returns `false` to stop. Returns an id. |
| `clock().remove(id)` | Stops it. |

The clock ticks only while a function is added, so an idle app still sleeps. There is no
"every N seconds" timer: compare `now` with the last time inside `fn`, or let a worker thread
sleep and post.

```cpp
struct Clock {
    core::Field<ui::Label<>> time{};

    void attach(app::AppContext& ctx) {
        ctx.clock().add([this](auto) {
            set_value(time, std::format("{:%T}", std::chrono::system_clock::now()));
            return true;   // keep ticking
        });
    }
};
```

### Windows and theme

| Call | What it does |
|---|---|
| `open_window(model, config)` | Opens a window showing `model`; returns its id, 0 if none opened. See [Windows](windows.md). |
| `window_open(id)` | Whether that window is still open. |
| `close_window(id)` | Closes a window `open_window` opened. |
| `set_theme(theme)` | Every window takes `theme`. See [Themes](themes.md). |
| `follow_system_theme()` | Light or dark as the system is set, and when it changes. |
| `system_prefers_dark()` | What the system asks for. |
| `theme()` | The current theme. |
| `remember_window_size(geometry)` | Keeps an `app::WindowGeometry` at the main window's size and place. `app::run` does it for you with settings. |

### The platform

| Call | What it does |
|---|---|
| `choose_files(dialog, done)` | Shows the system's file dialog and returns at once. `done(result)` runs on the logic thread. |
| `show_message(kind, title, text)` | The system's message box: `MessageKind::Info`, `Warning` or `Error`. Ignored where there is none. |
| `on_file_drop(fn)` | `fn(drop)` runs on the logic thread for each file dropped on a window, after the widget under it saw it. `drop.paths` lists the files, `drop.position` is where. |
| `set_global_key_handler(fn)` | `fn(key)` sees every key press, shortcuts included, before the focused widget. It does not consume them. |

A file dialog:

```cpp
ctx.choose_files({.kind = app::FileDialogKind::Open, .title = "Open",
                  .filters = {{"Text files", "txt;md"}, {"All files", "*"}}},
                 [&](app::FileDialogResult r) {
                     if (!r.error.empty()) ctx.show_message(app::MessageKind::Error, "Open", r.error);
                     else if (!r.paths.empty()) open_file(r.paths.front());
                     // empty paths and no error: the user cancelled
                 });
```

| `app::FileDialog` member | Meaning |
|---|---|
| `kind` | `Open` (one file), `OpenMany`, `Save` or `Folder`. |
| `title` | The dialog's title. |
| `filters` | `{name, patterns}` pairs; patterns are extensions separated by `;`, or `*`. |
| `start` | The folder (or the file, for `Save`) to start from. Empty: the system chooses. |

## Pitfalls

- Everything here except `post`, `poster()` and `waker()` belongs to the logic thread.
- `receive` replaces any waker already set on that `Shared` or `Channel`: call it once per source.
- `choose_files` does not block. Code after it runs before the user has chosen.
