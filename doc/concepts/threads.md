# Threads

Slow work belongs on a thread of its own, so the window stays responsive.
This page gives the one rule about threads, and the four ways a thread hands results to the model.

## The rule

While the app runs, the model lives on one thread, the **logic thread**.
`app::run` starts it, and runs your `view()`, your reactions and `attach` there.
The thread that called `app::run` handles the operating system's events and draws the window.
Every `Field`, `State`, `Derived` and `List`, and every widget, belongs to the logic thread.
Before `app::run` starts and after it returns, the model is the calling thread's again.
They are not thread-safe: setting one from another thread is a data race.
A debug build asserts when it happens:

```text
a model field set off the app's logic thread: hand it over with AppContext::post, Shared<T> or Channel<T>
```

The drawing thread never runs your code.
So a slow function on the logic thread freezes the *content* of the window, but never the window:
it still moves, resizes and repaints.

## Four ways across

| Type or call | Carries | When several arrive before the logic thread looks |
|---|---|---|
| `core::Shared<T>` | the newest value | only the newest is delivered |
| `core::Channel<T>` | every value, in order | all are delivered, in order |
| `ctx.post(fn)` / `ctx.poster()` | a function to run on the logic thread | each runs, in order |
| `core::Latest<T>` | the newest value, never locking or allocating | the reader sees the newest |

Pick by what a lost value would mean.
A temperature reading that is replaced by the next one: `Shared`.
A log line, a key press, a finished job: `Channel` or `post`.
An audio callback that must never wait: `Latest`.

## A worker with `Shared<T>`

```cpp
struct Thermometer {
    core::Field<ui::Label<>> text{{"waiting..."}};
    core::Shared<double> celsius{0.0};

    // Declared last: it stops before the members it writes to are destroyed.
    std::jthread sampler{[this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            celsius = read_sensor();   // any thread may set a Shared
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }};

    void attach(app::AppContext& ctx) {
        ctx.receive(celsius, [this](double c) { set_value(text, std::format("{:.1f} °C", c)); });
    }

    void view(app::ViewBuilder& vb) { vb.widget(text); }
};
```

1. The sampler thread sets `celsius`. Setting it wakes the logic thread if it sleeps.
2. The logic thread calls the function given to `ctx.receive` with the newest value.
3. That function runs on the logic thread, so it may change the label.

If the sampler sets ten values while the logic thread is busy, the function runs once, with the
tenth. A slow window drops old readings instead of queueing them.

`attach` runs once, before the app starts. [Blocks](blocks.md) explains it.

:::{note}
A `Shared<T>` placed with `vb.widget(celsius)`, or shown by reflection, is shown as read-only text
and updates on its own, with no `receive`.
:::

## Every event with `Channel<T>`

A `Channel<T>` delivers every value sent, in the order sent:

```cpp
struct BuildLog {
    core::List<std::string> lines;
    core::Channel<std::string> output;

    std::jthread build{[this](std::stop_token) {
        for (auto& line : run_build()) output.send(line);   // any thread may send
    }};

    void attach(app::AppContext& ctx) {
        ctx.receive(output, [this](const std::string& line) { lines.push_back(line); });
    }

    void view(app::ViewBuilder& vb) { vb.list(lines).follow_tail(); }
};
```

A channel has no current value, only arrivals.
It is unbounded: if the logic thread stalls, the queue grows. For a flood of small events,
send batches: the task runner sends 4,096 lines or 16 ms at a time, whichever comes first.

## Run a function on the logic thread

`ctx.post(fn)` runs `fn` on the logic thread, then refreshes the window.
Called on the logic thread itself, it runs `fn` right away.
`ctx.poster()` returns a function a worker can keep and call later:

```cpp
struct Report {
    core::Field<ui::Label<>> status{{"working..."}};
    std::jthread job;

    void attach(app::AppContext& ctx) {
        job = std::jthread{[this, post = ctx.poster()] {
            auto result = long_computation();
            post([this, result] { set_value(status, "done: " + result); });
        }};
    }

    void view(app::ViewBuilder& vb) { vb.widget(status); }
};
```

A function given to the poster after the app closed is dropped, never run.
So a worker that outlives the window does no harm.

## Real-time threads: `Latest<T>`

An audio callback or a data-acquisition interrupt must never wait for a lock or allocate.
`Shared<T>` allocates on each set, and `Channel<T>` on each send.
`core::Latest<T>` does neither: one writer, one reader, a copy and an atomic swap:

```cpp
struct Peak { float left = 0, right = 0; };   // trivially copyable

core::Latest<Peak> peaks;

// in the audio callback:
peaks.publish(Peak{l, r});

// on the logic thread, from a clock tick:
Peak p = peaks.read();
```

The reader always gets a whole value, the newest one published, never a half-written one.
The logic thread reads it from a timer tick, or a normal-priority thread forwards it into a `Shared`.
The synth rack's audio thread works this way.

## Thread lifetime

A `std::jthread` member asks its thread to stop and joins it when the struct is destroyed.
Declare it **last**, so it stops before the members it writes to are destroyed:
members are destroyed in reverse order of declaration.

## Without reflection

`ctx.receive` works with every compiler. It is the portable way.

With reflection, PRISM also wires `Shared` and `Channel` members on its own: those shown by the
view update their widget, and the others are delivered to their `observe` callbacks.
Code that must build in the browser or with Clang uses `ctx.receive`.

## Errors in callbacks

An exception that escapes a callback on the logic thread does not stop the app.
It goes to the error handler, which prints it by default. Replace it to log or show it:

```cpp
core::set_unhandled_error_handler([](std::exception_ptr e) {
    try { std::rethrow_exception(e); }
    catch (const std::exception& ex) { log_error(ex.what()); }
});
```
