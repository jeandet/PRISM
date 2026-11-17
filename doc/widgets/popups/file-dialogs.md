# File dialogs and message boxes

Picking a file and showing an alert are the platform's job: PRISM asks the operating system
for its own dialogs. Both go through the app's context, `app::AppContext`, which a
[block's `attach`](../../concepts/blocks.md) or the setup function of `app::run` receives.

```cpp
struct Notes {
    core::Field<ui::Button> open{{"Open…"}};
    core::Field<ui::Label<>> path{{"No file"}};
    app::AppContext* ctx = nullptr;
    core::Wiring wire;

    void attach(app::AppContext& c) {
        ctx = &c;
        wire.on(open, [this](const auto&) { choose(); });
    }

    void choose() {
        ctx->choose_files({.kind = app::FileDialogKind::Open, .title = "Open",
                           .filters = {{"Text files", "txt;md"}, {"All files", "*"}}},
                          [this](app::FileDialogResult r) {
                              if (!r.paths.empty()) path = {r.paths.front()};
                          });
    }

    void view(app::ViewBuilder& vb) { vb.vstack(open, path); }
};
```

## File dialogs

`ctx.choose_files(dialog, done)` shows the platform's file dialog. It never blocks:
`done` runs later, on the logic thread, with the answer.

`app::FileDialog`:

| Member | Type | Default | Meaning |
|---|---|---|---|
| `kind` | `app::FileDialogKind` | `Open` | `Open`, `OpenMany`, `Save` or `Folder` |
| `title` | `std::string` | `""` | the dialog's title |
| `filters` | `std::vector<app::FileFilter>` | none | each `{name, patterns}`: `{"Images", "png;jpg"}`; `"*"` for any file |
| `start` | `std::string` | `""` | the folder (or the file, for Save) to start from; empty: the platform's choice |

`app::FileDialogResult`:

| Member | Type | Meaning |
|---|---|---|
| `paths` | `std::vector<std::string>` | the chosen paths; empty when cancelled or failed |
| `error` | `std::string` | why it failed, when it did |

In the browser there is no file dialog: `done` gets an error.

## Message boxes

`ctx.show_message(kind, title, text)` shows the platform's message box, for what the app
cannot show in its own window. `kind` is `app::MessageKind::Info`, `Warning` or `Error`.
Inside the window, a [dialog](dialog.md) is usually the better choice.

## Files dropped on the window

`ctx.on_file_drop(fn)` calls `fn` on the logic thread each time the user drops files on the
window. `fn` gets an `input::FileDrop`: its `paths`, and the `position` where they landed.

```cpp
ctx.on_file_drop([this](const input::FileDrop& drop) { open(drop.paths.front()); });
```

## See also

- [Dialog](dialog.md): a dialog drawn by PRISM, inside the window.
