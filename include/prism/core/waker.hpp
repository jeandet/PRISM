#pragma once

#include <prism/core/atomic_cell.hpp>

#include <functional>

namespace prism::core {

// A callback that cross-thread state (Shared<T>, Channel<T>) fires when it has something new to
// deliver, so the thread that drains it can be woken. Set and fired from any thread.
class Waker {
    atomic_cell<std::function<void()>> fn_;

public:
    void set(std::function<void()> fn) { fn_.store(std::move(fn)); }

    void operator()() const {
        if (auto fn = fn_.load(); fn && *fn) (*fn)();
    }
};

} // namespace prism::core
