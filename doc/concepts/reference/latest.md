# Latest

`#include <prism/prism.hpp>` · namespace `prism::core`

`Latest<T>` hands the newest value from one real-time thread to one reader, without ever locking
or allocating. It is for an audio callback or an acquisition interrupt, where waiting even
a microsecond is a glitch.

## Example

```cpp
struct Peak {
    float left = 0, right = 0;
};

core::Latest<Peak> peaks;

// the audio thread, every buffer:
void on_audio(std::span<const float> l, std::span<const float> r) {
    peaks.publish(Peak{max_abs(l), max_abs(r)});
}

// the reader, from a clock tick on the logic thread:
ctx.clock().add([&](auto) {
    Peak p = peaks.read();
    set_value(meter, p.left);
    return true;   // keep ticking
});
```

## Members

| Member | Thread | Meaning |
|---|---|---|
| `Latest<T> l;` / `Latest<T> l{v};` | | holds `T{}` / `v` |
| `publish(v)` | the one writer | makes `v` the newest value; never blocks, never allocates |
| `read()` | the one reader | the newest value published, or the last one read if nothing new came |

## How it works

Three slots take turns: the writer fills one, the reader reads another, and the third sits in
between, holding the newest complete value. Publishing is a copy and one atomic swap.
The reader always gets a whole value, never a half-written one.

## Rules

- **One writer and one reader.** Two writers, or two readers, corrupt it.
- **`T` must be trivially copyable** (checked at compile time): plain numbers and structs of them.
- **It does not wake anyone.** The reader polls: from a [clock tick](app.md#appcontext), or from a
  normal thread that forwards values into a [`Shared`](shared.md).
