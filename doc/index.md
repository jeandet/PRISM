# PRISM

**Persistent Rendering & Interactive Scene Model.**
A from-scratch C++26 GUI framework. Write a struct, get a UI.
The renderer runs on its own thread, so slow app code never freezes the window.

:::{warning}
PRISM is an R&D project. Nothing here is production-ready yet.
:::

```cpp
#include <prism/prism.hpp>
using namespace prism;

struct Counter {
    core::Field<int> count{42};
    core::Field<std::string> label{"Hello, PRISM!"};
};

int main() {
    Counter counter;
    return app::run(counter, {.title = "My App"});   // C++26 reflection builds the UI
}
```

<img src="screenshots/counter.svg" alt="The counter app: a number and a text field" width="400"/>

**Try it without installing anything:** the [live demos](https://jeandet.github.io/PRISM/) run the
real examples, compiled to WebAssembly.

Where to start:

- New here? Read [Why PRISM](start/why.md), then [Your first app](start/first-app.md).
- Want to build it? [Install](start/install.md).
- Looking for a widget? The [widget reference](widgets/index.md) has every one, with an example;
  the [widget gallery](https://jeandet.github.io/PRISM/) lets you try them in the browser.

```{toctree}
:caption: Getting started
:maxdepth: 1

start/why
start/install
start/first-app
start/examples
```

```{toctree}
:caption: Concepts
:maxdepth: 2

concepts/model
concepts/views
concepts/blocks
concepts/threads
concepts/reference/index
```

```{toctree}
:caption: Widgets
:maxdepth: 2

widgets/index
```

```{toctree}
:caption: The app
:maxdepth: 1

app/windows
app/themes
app/settings
app/inspector
app/app-context
```

```{toctree}
:caption: Integrations
:maxdepth: 1

integrations/http-and-io
integrations/audio
integrations/python
integrations/python-reference
```

```{toctree}
:caption: Project
:maxdepth: 1

project/roadmap
```
