# Shared

`#include <prism/prism.hpp>` · namespace `prism::core`

`Shared<T>` carries the newest value from any thread to the logic thread.
A worker sets it as often as it likes; the logic thread gets the newest value when it looks.
Values set in between are dropped, so a slow window never falls behind.

## Example

```cpp
struct Cpu {
    core::Field<ui::ProgressBar> load{{.label = "CPU"}};
    core::Shared<double> sample{0.0};

    std::jthread sampler{[this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            sample = read_cpu_load();   // any thread
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }};

    void attach(app::AppContext& ctx) {
        ctx.receive(sample, [this](double v) { set_value(load, v); });   // on the logic thread
    }

    void view(app::ViewBuilder& vb) { vb.widget(load); }
};
```

## Members

| Member | Thread | Meaning |
|---|---|---|
| `Shared<T> s;` / `Shared<T> s{v};` | | holds `T{}` / `v` |
| `set(v)` | any | stores `v` as the newest value; wakes the logic thread if no value was pending |
| `s = v` | any | the same as `set(v)` |
| `get()` | any | a copy of the newest value |
| `observe(fn)` | logic | calls `fn(const T&)` with each delivered value, for as long as `s` lives |
| `on_change()` | logic | its signal; `connect(fn)` returns a [`Connection`](signals.md) |
| `set_waker(fn)` | setup | called on the first `set` after a delivery; `ctx.receive` sets it for you |
| `drain_notifications()` | logic | delivers the pending value, if any; `ctx.receive` calls it for you |

## Delivering to the logic thread

`ctx.receive(shared, fn)` ([AppContext](app.md#appcontext)) does the wiring: `fn` runs on the logic
thread with the newest value after each wake. Call it in a block's `attach` or in the app's setup.

Shown in a view with `vb.widget(shared)` (or by reflection), a `Shared` is read-only text that
updates on its own.

## How values are dropped

1. The worker sets 1, then 2, then 3, while the logic thread is busy.
2. The first `set` wakes the logic thread; the next two only replace the value.
3. The logic thread wakes, and delivers 3, once.

## Pitfalls

- **No `+=`.** Two threads doing `s = s.get() + 1` lose updates. A Shared has one writer at a
  time, or carries whole values. Counting events from several threads is a job for a `Channel`.
- **Each `set` allocates.** Fine for a worker; not for a real-time thread. Use [`Latest`](latest.md) there.
- **Not every value arrives.** If each one matters, use a [`Channel`](channel.md).
- **`T` is copied on `get()`.** Keep it to values, not large buffers that change often; for those,
  publish a `std::shared_ptr<const Buffer>`.
