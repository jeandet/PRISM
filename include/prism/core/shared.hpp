#pragma once

#include <prism/core/atomic_cell.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/waker.hpp>

#include <atomic>
#include <functional>
#include <vector>

namespace prism::core {

template <typename T>
struct Shared {
    Shared() : cell_(T{}) {}
    Shared(T init) : cell_(std::move(init)) {}

    T get() const {
        auto ptr = cell_.load();
        return ptr ? *ptr : T{};
    }

    void set(T new_value) {
        cell_.store(std::move(new_value));
        if (!pending_.exchange(true, std::memory_order_acq_rel))
            waker_();
    }

    // Called (from the setting thread) when a value becomes pending, so the draining thread
    // can be woken. Later set()s before the next drain coalesce and do not call it again.
    void set_waker(std::function<void()> fn) { waker_.set(std::move(fn)); }

    void drain_notifications() {
        if (!pending_.exchange(false, std::memory_order_acquire))
            return;
        changed_.emit(get());
    }

    SenderHub<const T&>& on_change() { return changed_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(changed_.connect(std::move(cb)));
    }

private:
    atomic_cell<T> cell_;
    std::atomic<bool> pending_{false};
    Waker waker_;
    SenderHub<const T&> changed_;
    std::vector<Connection> observers_;
};

} // namespace prism::core
