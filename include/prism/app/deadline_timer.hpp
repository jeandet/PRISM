#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace prism::app {

// Calls `fire` on its own thread once the earliest armed deadline has passed. The app arms it
// with the logic thread's next timed wake (a tooltip's delay) and `fire` posts that wake; the
// run loop itself has no timers. Each arm() fires at most once, then the timer idles until
// the next arm(). `fire` must not touch the timer.
class DeadlineTimer {
public:
    using clock = std::chrono::steady_clock;

    explicit DeadlineTimer(std::function<void()> fire)
        : fire_(std::move(fire)), thread_([this](std::stop_token st) { run(st); }) {}

    DeadlineTimer(const DeadlineTimer&) = delete;
    DeadlineTimer& operator=(const DeadlineTimer&) = delete;

    // Keeps the earlier of `at` and the deadline already pending.
    void arm(clock::time_point at) {
        {
            std::lock_guard lock(m_);
            if (due_ && *due_ <= at) return;
            due_ = at;
        }
        cv_.notify_one();
    }

private:
    void run(std::stop_token st) {
        std::unique_lock lock(m_);
        while (!st.stop_requested()) {
            if (!due_) {
                cv_.wait(lock, st, [&] { return due_.has_value(); });
                continue;
            }
            auto at = *due_;
            // Wakes early when arm() moves the deadline sooner, or on stop; the loop re-reads.
            if (cv_.wait_until(lock, st, at, [&] { return !due_ || *due_ < at; })) continue;
            if (st.stop_requested()) return;
            due_.reset();
            lock.unlock();
            fire_();
            lock.lock();
        }
    }

    std::function<void()> fire_;
    std::mutex m_;
    std::condition_variable_any cv_;
    std::optional<clock::time_point> due_;
    std::jthread thread_;   // last: started after, and joined before, everything it reads
};

} // namespace prism::app
