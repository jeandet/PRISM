<p align="center">
  <img src="doc/logo/logo.png" alt="PRISM logo" width="500"/>
</p>

<h3 align="center">Persistent Rendering &amp; Interactive Scene Model</h3>

<p align="center">
  A from-scratch C++26 GUI framework. Write a struct, get a UI.<br/>
  The renderer runs on its own thread, so slow app code never freezes the window.
</p>

<p align="center">
  <a href="https://github.com/jeandet/PRISM/actions/workflows/ci.yml"><img src="https://github.com/jeandet/PRISM/actions/workflows/ci.yml/badge.svg" alt="CI"/></a>
  <a href="https://jeandet.github.io/PRISM/"><img src="https://img.shields.io/badge/live%20demos-in%20your%20browser-8250df" alt="Live demos"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-26%20%7C%2023-0969da" alt="C++26 | C++23"/>
  <img src="https://img.shields.io/badge/license-MIT-1a7f37" alt="MIT license"/>
</p>

> [!WARNING]
> PRISM is an R&D project. Nothing here is production-ready yet.

```cpp
#include <prism/prism.hpp>
using namespace prism;   // the snippets below assume it: names read core::Field, ui::Slider, app::run

struct Counter {
    core::Field<int> count{42};
    core::Field<std::string> label{"Hello, PRISM!"};
};

int main() {
    Counter counter;
    return app::run(counter, {.title = "My App"});   // C++26 reflection builds the UI
}
```

<p align="center"><img src="doc/screenshots/counter.svg" alt="Counter app" width="400"/></p>

**Try it without installing anything:** the [live demos](https://jeandet.github.io/PRISM/) run the
real examples, compiled to WebAssembly.

## Why PRISM

Qt, GTK and wxWidgets run your code and the renderer on the same thread.
If one callback is slow, nothing else happens.
The window cannot repaint, cannot resize, and looks frozen.

PRISM puts the renderer on a separate thread.
Your code still blocks its own thread, but the window stays alive.

<p align="center"><img src="doc/diagrams/frame-budget.svg" alt="Timeline: a 40 ms callback freezes a single-threaded toolkit for two frames; in PRISM the render thread keeps serving OS events from the last snapshot" width="900"/></p>

This is an architectural choice, not a tuning trick. It is built in from day one.

## Documentation

The guide and the design notes live in [`doc/`](doc/index.md), and build into a Read the Docs site
(`doc/conf.py`).

| Start | Guide | |
|---|---|---|
| [Why PRISM and how it works](doc/guide/concepts.md) | [From a struct to a window](doc/guide/struct-to-ui.md) | [Documents, code and Markdown](doc/guide/documents.md) |
| [Building](doc/guide/building.md) | [Blocks](doc/guide/blocks.md) | [Settings that persist](doc/guide/settings.md) |
| [Examples](doc/guide/examples.md) | [Reactive core](doc/guide/reactive-core.md) | [Drag and drop, and docking](doc/guide/drag-and-dock.md) |
| [Python SDK](doc/guide/python.md) | [Canvas and plot](doc/guide/canvas-and-plot.md) | [Windows, themes and the inspector](doc/guide/windows-and-themes.md) |
| [Roadmap](doc/guide/roadmap.md) | [Table, tree and tabs](doc/guide/data-widgets.md) | [HTTP, TCP and serial](doc/guide/io.md) |
| [Design notes](doc/design/README.md) | [Text, editing and menus](doc/guide/text-and-menus.md) | |

## Examples

[`examples/tour/`](examples/tour/) is the place to start: five programs of 20 to 40 lines, one idea
each. [`examples/`](examples/README.md) holds eight complete apps, from a widget gallery to a
synthesizer and a SQL tool; all of them [run in the browser](https://jeandet.github.io/PRISM/).

<p align="center"><img src="doc/screenshots/widget_gallery.svg" alt="Widget Gallery" width="700"/></p>

## Building

You need **Meson ≥ 1.5** and a recent C++ compiler.
All dependencies are fetched as Meson wraps.

```bash
meson setup builddir
ninja -C builddir
meson test -C builddir
```

| Compiler | Reflection | What you write |
|---|---|---|
| GCC 16+ | yes, `-freflection` is auto-detected | nothing: structs become UIs |
| GCC 14+, Clang 17+ | no | one `view()` method per model |

The browser build, the WebGPU renderer and the four entry points are in
[Building](doc/guide/building.md).

## License

MIT
