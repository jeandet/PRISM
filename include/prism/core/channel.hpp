#pragma once

#include <prism/core/connection.hpp>
#include <prism/core/mpsc_queue.hpp>
#include <prism/core/waker.hpp>

#include <atomic>
#include <functional>
#include <vector>

namespace prism::core {

// Cross-thread, ordered, lossless event stream. Contrast with Shared<T>: Shared<T> publishes
// a latest value and coalesces intermediate set()s away, Channel<T> delivers every send() in
// FIFO order. No get() -- there is no "current value", only a sequence of arrivals.
template <typename T>
class Channel {
public:
    // Thread-safe -- may be called from any thread.
    void send(T value) {
        queue_.push(std::move(value));
        if (!wake_pending_.exchange(true, std::memory_order_acq_rel))
            waker_();
    }

    // Called (from the sending thread) on the first send() after a drain, so the draining
    // thread can be woken. Later sends before the next drain do not call it again.
    void set_waker(std::function<void()> fn) { waker_.set(std::move(fn)); }

    // Consumer-thread only. Pops everything queued so far, in order, and emits each via
    // on_receive() -- unlike Shared<T>::drain_notifications(), never coalesces.
    void drain_notifications() {
        // Cleared before popping, and with an RMW, not a plain store: the exchange reads the
        // last sender's `true` (through the release sequence), which makes that sender's push
        // visible to the pops below. A send() ordered after this exchange reads `false` and
        // wakes again. So every send is either popped here or causes a new wake.
        wake_pending_.exchange(false, std::memory_order_acq_rel);
        while (auto value = queue_.pop())
            received_.emit(*value);
    }

    SenderHub<const T&>& on_receive() { return received_; }

    void observe(std::function<void(const T&)> cb) {
        observers_.push_back(received_.connect(std::move(cb)));
    }

private:
    // simplify: mpsc_queue is unbounded -- a stalled consumer thread lets a producer grow it
    // without limit. Add a bounded/drop-oldest policy if the stall-latency work shows this
    // matters in practice.
    mpsc_queue<T> queue_;
    std::atomic<bool> wake_pending_{false};
    Waker waker_;
    SenderHub<const T&> received_;
    std::vector<Connection> observers_;
};

} // namespace prism::core
