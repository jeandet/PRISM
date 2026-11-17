# Channel

`#include <prism/prism.hpp>` · namespace `prism::core`

`Channel<T>` carries every value sent from any thread to the logic thread, in the order sent.
Nothing is dropped and nothing is merged. It has no current value, only arrivals.

## Example

```cpp
struct Console {
    core::List<std::string> lines;
    core::Channel<std::string> output;

    std::jthread reader{[this](std::stop_token stop) {
        std::string line;
        while (!stop.stop_requested() && std::getline(std::cin, line)) output.send(line);
    }};

    void attach(app::AppContext& ctx) {
        ctx.receive(output, [this](const std::string& l) { lines.push_back(l); });
    }

    void view(app::ViewBuilder& vb) { vb.list(lines).follow_tail(); }
};
```

## Members

| Member | Thread | Meaning |
|---|---|---|
| `send(v)` | any | queues `v`; wakes the logic thread on the first send after a delivery |
| `observe(fn)` | logic | calls `fn(const T&)` for each value delivered, for as long as the channel lives |
| `on_receive()` | logic | its signal; `connect(fn)` returns a [`Connection`](signals.md) |
| `set_waker(fn)` | setup | called on the first send after a delivery; `ctx.receive` sets it for you |
| `drain_notifications()` | logic | delivers everything queued, in order; `ctx.receive` calls it for you |

`ctx.receive(channel, fn)` ([AppContext](app.md#appcontext)) wires it: `fn` runs on the logic thread
once per value, in order.

## Shared or Channel

| | `Shared<T>` | `Channel<T>` |
|---|---|---|
| Delivers | the newest value | every value |
| Has a current value | yes, `get()` | no |
| If the logic thread is slow | older values are dropped | values queue up |
| Typical use | a reading, a position, a status | log lines, events, finished jobs |

## Pitfalls

- **The queue is unbounded.** A producer much faster than the logic thread grows it without limit.
  Send batches (`Channel<std::vector<std::string>>`), as the task runner does: 4,096 lines or 16 ms,
  whichever comes first.
- **Each `send` allocates.** Not for a real-time thread: use [`Latest`](latest.md) there.
