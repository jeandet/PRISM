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
  <a href="https://prism-gui.readthedocs.io/"><img src="https://readthedocs.org/projects/prism-gui/badge/?version=latest" alt="Documentation"/></a>
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

The manual is on [prism-gui.readthedocs.io](https://prism-gui.readthedocs.io/en/latest/). It starts with a tour and ends with a
reference for every widget and type, each with an example. Its pages are Markdown in
[`doc/`](doc/index.md).

| Start | Concepts | Widgets | The app |
|---|---|---|---|
| [Why PRISM](https://prism-gui.readthedocs.io/en/latest/start/why.html) | [The model](https://prism-gui.readthedocs.io/en/latest/concepts/model.html) | [All widgets](https://prism-gui.readthedocs.io/en/latest/widgets/index.html) | [Windows](https://prism-gui.readthedocs.io/en/latest/app/windows.html) |
| [Install](https://prism-gui.readthedocs.io/en/latest/start/install.html) | [Views and layout](https://prism-gui.readthedocs.io/en/latest/concepts/views.html) | [Controls](https://prism-gui.readthedocs.io/en/latest/widgets/controls.html) | [Themes](https://prism-gui.readthedocs.io/en/latest/app/themes.html) |
| [Your first app](https://prism-gui.readthedocs.io/en/latest/start/first-app.html) | [Blocks](https://prism-gui.readthedocs.io/en/latest/concepts/blocks.html) | [Tables, trees, lists](https://prism-gui.readthedocs.io/en/latest/widgets/data.html) | [Settings](https://prism-gui.readthedocs.io/en/latest/app/settings.html) |
| [Examples](https://prism-gui.readthedocs.io/en/latest/start/examples.html) | [Threads](https://prism-gui.readthedocs.io/en/latest/concepts/threads.html) | [Text and code](https://prism-gui.readthedocs.io/en/latest/widgets/text.html) | [Python](https://prism-gui.readthedocs.io/en/latest/integrations/python.html) |

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
[Install](https://prism-gui.readthedocs.io/en/latest/start/install.html).

## License

MIT
