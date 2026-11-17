# Building

You need **Meson ≥ 1.5** and a recent C++ compiler.
All dependencies (SDL3, stdexec, doctest, fmt, magic_enum) are fetched as Meson wraps.

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

| Compiler | Reflection | What you write |
|---|---|---|
| GCC 16+ | yes, `-freflection` is auto-detected | nothing: structs become UIs |
| GCC 14+, Clang 17+ | no | one `view()` method per model |

<details>
<summary><b>Language features PRISM relies on</b></summary>

| Feature | Used for | Required |
|---|---|---|
| Static reflection (P2996) | UI generated from structs | no, `view()` is the fallback |
| `std::execution` (P2300) | `run_loop` event loop, `core::then` / `core::on` adaptors | yes, via stdexec |
| Concepts | widget resolution, strong geometry types | yes |
| `std::expected` | fallible APIs, no exceptions at the boundary | yes |
| Designated initialisers | named-parameter widget construction | yes |
| magic_enum | enum dropdowns without reflection | only without P2996 |

</details>

## Four ways in

| Entry point | You get | Example |
|---|---|---|
| `app::run(model, {.title, .width, .height})` | UI from your model structs (the main API) | every app in [`examples/`](../../examples/) |
| `app::model_app(backend, window, model)` | the same, on a backend and window you made | the WebGPU renderer below |
| `app<State>(title, state, view, update)` | manual `row()` / `column()` layout | `tests/test_ui.cpp` |
| `app::App` + `Frame` | a raw `DrawList`, nothing else | |

Each takes the window's title and size. `decoration` picks the window chrome:

```cpp
app::run(dashboard, {.title = "My App", .width = 900, .height = 600,
                     .decoration = app::DecorationMode::Custom});
```

- `Custom` (default): PRISM draws the title bar and resize edges. Looks the same everywhere, including Wayland.
- `Native`: the OS window manager draws it.
- `None`: a borderless window.

## WebGPU renderer (experimental)

By default PRISM draws with SDL's renderer. Build with `-Dwebgpu=enabled` to add a WebGPU
renderer (a prebuilt [wgpu-native](https://github.com/gfx-rs/wgpu-native), Linux and macOS),
then pick it per app or per run:

```bash
meson setup builddir -Dwebgpu=enabled
PRISM_RENDERER=webgpu ./builddir/examples/model_notes/model_notes
```

```cpp
auto backend = app::Backend::software({.renderer = app::Renderer::WebGpu});
auto& window = backend.create_window({.title = "Notes"});
app::model_app(backend, window, notes);
```

Windows, input and the clipboard stay SDL's; only the drawing changes. Without a GPU, Mesa's
software Vulkan (lavapipe) works too. A window that cannot get a WebGPU surface falls back to
SDL's renderer with a warning. Status and plan: [`doc/design/second-backend.md`](../design/second-backend.md).

## In the browser

The same code builds for the web with [Emscripten](https://emscripten.org) 6.0.10+.
Upstream Clang has no P2996, so models need a `view()` method there.

```bash
source /path/to/emsdk/emsdk_env.sh
meson setup builddir-wasm --cross-file cross/wasm32-emscripten.ini
ninja -C builddir-wasm
meson test -C builddir-wasm                 # tests run under node
emrun builddir-wasm/examples/widget_gallery/widget_gallery.html
```

Anything linking `prism_dep` gets the browser setup for free: SDL3 from Emscripten ports, the
fonts bundled in, and JSPI so the event loop can wait without freezing the page.
Two requirements:

- PRISM uses threads, so the page needs COOP/COEP headers. `emrun` sends them.
- JSPI needs Chrome 137+, Firefox 153+ or Safari 27.

The [live demos](https://jeandet.github.io/PRISM/) are built by `.github/workflows/pages.yml`.
`scripts/build_web_site.sh` builds the same site locally.
