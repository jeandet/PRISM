#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <version>

namespace prism::core {

#if defined(__cpp_lib_atomic_shared_ptr)
template <typename T>
using atomic_shared_ptr = std::atomic<std::shared_ptr<T>>;
#else
// simplify: libc++ (and so Emscripten) has no std::atomic<shared_ptr> yet (P0718,
// llvm/llvm-project#99980). This stand-in guards the pointer with a mutex; libstdc++'s own
// std::atomic<shared_ptr> is lock-based too, so nothing is lost. Delete once libc++ ships it.
template <typename T>
class atomic_shared_ptr {
    mutable std::mutex mutex_;
    std::shared_ptr<T> ptr_;

public:
    atomic_shared_ptr() = default;
    atomic_shared_ptr(std::shared_ptr<T> p) : ptr_{std::move(p)} {}

    void store(std::shared_ptr<T> p, std::memory_order = std::memory_order_seq_cst) {
        std::lock_guard lock{mutex_};
        ptr_.swap(p);
        // p (the old value) is released after the unlock, so a destructor never runs under the lock.
    }

    [[nodiscard]] std::shared_ptr<T> load(std::memory_order = std::memory_order_seq_cst) const {
        std::lock_guard lock{mutex_};
        return ptr_;
    }
};
#endif

// Single-value atomic cell for publishing immutable snapshots.
// Multiple producers may store; a single consumer loads the latest.
// Intermediate values are silently dropped (by design).
template <typename T>
class atomic_cell {
    atomic_shared_ptr<const T> ptr_{nullptr};

public:
    atomic_cell() = default;

    explicit atomic_cell(T value)
        : ptr_{std::make_shared<const T>(std::move(value))}
    {}

    void store(T value)
    {
        ptr_.store(std::make_shared<const T>(std::move(value)));
    }

    [[nodiscard]] std::shared_ptr<const T> load() const
    {
        return ptr_.load();
    }
};

} // namespace prism::core
