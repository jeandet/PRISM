# Python

PRISM has a Python SDK with the same model as C++: a class of fields becomes a window. Any
Python thread may change a field, and the window follows. This page is the tour; the
[Python reference](python-reference.md) lists every function.

## Build and run

The SDK is a compiled extension built with the rest of PRISM:

```bash
meson setup builddir
ninja -C builddir
PYTHONPATH=builddir/python python3 python/examples/01_counter.py
```

To install it in a virtual environment instead:

```bash
pip install . --no-build-isolation
```

## A model is a window

```python
import prism

class Mixer(prism.Model):
    volume = prism.slider(0.75, min=0.0, max=1.0, label="Volume")
    mute = prism.checkbox(False, label="Mute")
    status = prism.derived(lambda self: "muted" if self.mute.value else f"{self.volume.value:.0%}")

m = Mixer()
prism.run(m, title="Mixer")
```

- `prism.slider` and `prism.checkbox` declare fields with a widget. The class lists them; each
  instance gets its own.
- `prism.derived` declares a value computed from other fields. It follows what the function
  reads, and recomputes when one of them changes.
- `prism.run` opens the window and returns when the user closes it.

With no `view` method, the fields are stacked in the order they are declared.

## Laying out: view()

A `view(self, vb)` method places the widgets. `vb` is a `ViewBuilder`:

```python
class Mixer(prism.Model):
    volume = prism.slider(0.75, label="Volume")
    mute = prism.checkbox(False, label="Mute")
    reset = prism.button("Reset")

    def view(self, vb):
        vb.hstack(self.volume, self.mute)   # side by side
        vb.widget(self.reset)               # below them
```

`hstack` and `vstack` also take a function, to nest containers:

```python
    def view(self, vb):
        vb.vstack(lambda: (
            vb.group("Output", lambda: vb.hstack(self.volume, self.mute)),
            vb.widget(self.reset),
        ))
```

## Reading, writing, reacting

Every field handle has a `.value`. Reading it gives the value, assigning it changes it, from any
thread:

```python
m.volume.value = 0.5
print(m.volume.value)
```

`observe(callback)` runs the callback on each change, on the logic thread:

```python
def on_reset(clicks):          # a button's value counts its clicks
    m.volume.value = 0.75

conn = m.reset.observe(on_reset)
```

Inside the class, `@prism.on_change` does the same for a method:

```python
class Mixer(prism.Model):
    volume = prism.slider(0.75)
    level = prism.progress(0.75, label="Level")

    @prism.on_change(volume, immediate=True)
    def show_level(self):
        self.level.value = self.volume.value
```

`immediate=True` also runs it once when the model is made.

## Threads

PRISM has one logic thread. It owns the fields, runs the observers, and builds the frames. Your
Python threads talk to it:

| You do | It happens |
|---|---|
| `m.f.value = x` from any thread | The write is posted to the logic thread. |
| `m.f.value` from any thread | The read asks the logic thread and waits for the answer. |
| `m.f.add(n)` | An increment done on the logic thread: safe from many threads at once. |
| `with prism.transaction():` | The writes inside reach the observers together, once. |

Never write `m.f.value += 1` from a worker: two threads can read the same old value. Use
`m.f.add(1)`.

A worker that feeds the window:

```python
import random
import prism

class Sensor(prism.Model):
    temperature = prism.shared(20.0)            # latest value wins
    events = prism.channel(0)                   # every value, in order
    log = prism.list_field([])

    def view(self, vb):
        vb.widget(self.temperature)
        vb.list(self.log, follow_tail=True)

m = Sensor()
m.events.observe(lambda n: m.log.push(f"event {n}"))

def tick(stop):
    n = 0
    while not stop.wait(0.3):
        m.temperature.value = 20.0 + random.uniform(-2, 8)
        m.events.send(n)
        n += 1

prism.worker(tick)
prism.run(m, title="Sensor")
```

- `prism.shared` keeps the latest value: if the worker writes faster than the screen, the
  values in between are dropped.
- `prism.channel` keeps every value, in order.
- `prism.worker(fn)` runs `fn` on a thread of its own. `fn` may take a `stop` event; the worker
  is stopped when `prism.run` returns.

`prism.run` releases the GIL while the window is open, so your threads run alongside it. The
SDK also works on free-threaded Python (3.14t).

## Validation

A field can refuse values. With pydantic installed, an `Annotated` type does it:

```python
from typing import Annotated
from pydantic import Field as PField
import prism

Volume = Annotated[float, PField(ge=0, le=1)]

class Settings(prism.Model):
    volume: Volume = 0.75

m = Settings()
m.volume.value = 1.5   # raises: the value is above 1
```

## Testing without a window

`prism.headless` runs a model with no display, for tests and CI:

```python
with prism.headless(m) as app:
    m.volume.value = 0.2
    app.wait_until(lambda: m.status.value == "20%", timeout=2)
```

Or in one line: `prism.run(m, headless_seconds=1.0)`.

## Errors

An exception in an observer, a derived function or a worker does not stop the app. It goes to
the error handler, which prints the traceback by default:

```python
prism.on_error(lambda exc: print("oops:", exc))
```

## More examples

[`python/examples/`](../../python/examples/README.md) has nineteen runnable programs: a counter,
validation, threads, lists, live plots, a file tree, a dashboard, a worker pool, asyncio,
pickers, table and tree editing, docking, a lab instrument, a task runner, a SQL tool and a
Markdown notes app.

## Pitfalls

- `prism.field("text")` shows the text; it is not an input. For text the user types, use
  `prism.text_field` (one line), `prism.text_area` (several) or `prism.combobox` (one line,
  with suggestions).
- An observer lives as long as the handle it was registered on, or its model. Keep the model
  alive; `conn.disconnect()` ends one early.
- `wait_until` must not be called from an observer: it would wait for the thread it blocks.
- HTTP, sockets and audio have no Python binding: use `requests`, `socket` or a sound library.
