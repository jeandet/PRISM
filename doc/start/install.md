# Install and build

This page gets PRISM built and its examples running, on the desktop and in the browser.

## What you need

- [Meson](https://mesonbuild.com) 1.5 or newer, and Ninja.
- A C++ compiler from the table below.
- Nothing else: Meson fetches every dependency (SDL3, stdexec, fmt, doctest, magic_enum, Glaze).

| Compiler | Reflection | What you write |
|---|---|---|
| GCC 16 or newer | yes, found by the build | nothing: a struct becomes a window |
| GCC 14+, Clang 17+ | no | a short `view()` method per struct |
| Emscripten 6.0.10+ (the browser) | no | a short `view()` method per struct |

Reflection is C++26 static reflection (P2996).
Without it, everything still works: you place the fields yourself in `view()`.
[Your first app](first-app.md) shows both.

## Build and test

```bash
git clone https://github.com/jeandet/PRISM.git
cd PRISM
meson setup builddir
ninja -C builddir
meson test -C builddir
```

Then run an example:

```bash
./builddir/examples/tour/01_struct_to_ui
./builddir/examples/widget_gallery/widget_gallery
```

## Build options

Pass them to `meson setup`, as in `meson setup builddir -Dexamples=disabled`.

| Option | Default | What it builds |
|---|---|---|
| `examples` | auto | the example apps |
| `tests` | auto | the C++ and Python test suites |
| `benchmarks` | auto | the benchmarks |
| `http` | auto | `prism-net`: HTTP requests (not in the browser) |
| `io` | auto | `prism-io`: TCP and serial ports (not in the browser) |
| `markdown` | auto | `prism-markdown`: Markdown into a document |
| `webgpu` | disabled | the WebGPU renderer, see below |
| `prism_debug_tools` | auto | the live inspector on Ctrl+Shift+I |

## Use PRISM in your project

Put PRISM in your project's `subprojects/` folder (a git submodule, or a wrap file), then:

```meson
project('my_app', 'cpp', default_options : ['cpp_std=c++26'])
prism_dep = subproject('prism').get_variable('prism_dep')
executable('my_app', 'main.cpp', dependencies : prism_dep)
```

`prism_dep` brings the whole framework and the default window backend.
Your code includes one header:

```cpp
#include <prism/prism.hpp>
```

## In the browser

The same code builds for the web with [Emscripten](https://emscripten.org).
Upstream Clang has no reflection, so each struct needs a `view()` there.

```bash
source /path/to/emsdk/emsdk_env.sh
meson setup builddir-wasm --cross-file cross/wasm32-emscripten.ini
ninja -C builddir-wasm
meson test -C builddir-wasm        # the tests run under node
emrun builddir-wasm/examples/widget_gallery/widget_gallery.html
```

Anything that links `prism_dep` gets the browser setup for free:
SDL3 from Emscripten's ports, the fonts bundled in, and JSPI so the event loop can wait without
freezing the page.

Two things the page needs:

- PRISM uses threads, so the server must send the COOP and COEP headers. `emrun` does.
- JSPI needs Chrome 137+, Firefox 153+ or Safari 27.

The [live demos](https://jeandet.github.io/PRISM/) are this build of the examples.
`scripts/build_web_site.sh` builds the same site on your machine.

## The WebGPU renderer (experimental)

PRISM draws with SDL's renderer by default.
A build with `-Dwebgpu=enabled` adds a WebGPU renderer, on Linux and macOS.
Pick it for one run with an environment variable:

```bash
meson setup builddir -Dwebgpu=enabled
PRISM_RENDERER=webgpu ./builddir/examples/model_notes/model_notes
```

Or pick it in code, on a backend you create yourself:

```cpp
auto backend = app::Backend::software({.renderer = app::Renderer::WebGpu});
auto& window = backend.create_window({.title = "Notes"});
app::model_app(backend, window, notes);
```

Only the drawing changes: windows, input and the clipboard stay SDL's.
Without a GPU, Mesa's software Vulkan (lavapipe) works too.
A window that cannot get a WebGPU surface falls back to SDL's renderer, with a warning.

## The C++ features PRISM uses

| Feature | Used for | Required |
|---|---|---|
| Static reflection (P2996) | a window from a struct | no: `view()` is the fallback |
| `std::execution` (P2300) | the event loop | yes, through stdexec |
| Concepts | picking a widget for a type, geometry types | yes |
| `std::expected` | errors returned, not thrown | yes |
| Designated initializers | `{.value = 0.5, .max = 1.0}` | yes |
| magic_enum | a dropdown for an enum | only without reflection |
