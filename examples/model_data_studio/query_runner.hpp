#pragma once

#include <prism/core/channel.hpp>
#include <prism/core/shared.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sql/engine.hpp"

// The engine on a worker thread. This is the only file in the studio that knows both a thread and
// a PRISM type: the query itself is sql/engine.hpp, and everything here is the pipe that carries a
// request in and results out to the logic thread, which wires both ends with ctx.receive().
namespace prism::data_studio {

// How often SQLite calls back while a query runs (VM instructions per call), and how often that
// callback may publish: 50 ms is fast enough for a live status line, slow enough not to flood the
// channel.
inline constexpr int kProgressInstructions = 2'000;
inline constexpr auto kProgressPublish = std::chrono::milliseconds{50};

struct RunnerEvent {
    enum class Kind : std::uint8_t { Loading, Ready, Running };

    Kind kind = Kind::Loading;
    std::size_t done = 0;    // Loading: trips inserted so far
    std::size_t total = 0;   // Loading: trips to insert
    double elapsed = 0.0;    // Running: seconds the current query has been going
    bool operator==(const RunnerEvent&) const = default;
};

class QueryRunner {
public:
    explicit QueryRunner(std::vector<Trip> trips) : trips_(std::move(trips)) {
        thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }

    ~QueryRunner() {
        thread_.request_stop();
        request_ready_.notify_all();
        if (auto* db = database_handle_.load()) sqlite3_interrupt(db);
    }

    QueryRunner(const QueryRunner&) = delete;
    QueryRunner& operator=(const QueryRunner&) = delete;

    core::Shared<QueryOutcome>& results() { return results_; }
    core::Channel<RunnerEvent>& events() { return events_; }
    // The app no longer calls this: ctx.receive() drains each pipe when the worker wakes the logic
    // thread. Kept for a caller that owns the logic thread itself with no AppContext to wire the
    // waker (tests/test_data_studio.cpp polls it).
    void drain() {
        events_.drain_notifications();
        results_.drain_notifications();
    }

    // Replaces whatever was waiting. A query already running notices within a few thousand
    // instructions and gives way instead of finishing.
    void request(std::string sql, std::uint64_t generation) {
        cancel_requested_.store(false, std::memory_order_relaxed);
        {
            std::scoped_lock lock{request_mutex_};
            pending_ = Request{.sql = std::move(sql), .generation = generation};
        }
        request_ready_.notify_one();
    }

    // Safe from any thread: sqlite3_interrupt only sets a flag SQLite checks itself.
    void cancel() {
        cancel_requested_.store(true, std::memory_order_relaxed);
        if (auto* db = database_handle_.load()) sqlite3_interrupt(db);
    }

private:
    struct Request {
        std::string sql{};
        std::uint64_t generation = 0;
    };

    static std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }

    // True while a newer request waits, so the query in flight can step aside. Only the worker
    // calls this, and it never blocks: a request() holding the lock means "wait your turn".
    bool superseded() {
        std::unique_lock lock{request_mutex_, std::try_to_lock};
        return lock.owns_lock() && pending_.has_value();
    }

    static int progress_trampoline(void* self) {
        return static_cast<QueryRunner*>(self)->on_progress() ? 0 : 1;
    }

    // Runs on the worker thread, inside sqlite3_step. Returning false aborts the statement, which
    // read_all reports back as interrupted.
    bool on_progress() {
        if (!in_query_) return true;
        if (stop_.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) return false;
        if (superseded()) return false;
        auto stamp = now();
        if (stamp - last_publish_ < kProgressPublish) return true;
        last_publish_ = stamp;
        auto elapsed = std::chrono::duration<double>(stamp - query_started_).count();
        events_.send(RunnerEvent{.kind = RunnerEvent::Kind::Running, .elapsed = elapsed});
        return true;
    }

    void run(std::stop_token stop) {
        stop_ = stop;
        database_ = engine_detail::Database::open_memory();
        if (!database_.handle) return;
        database_handle_.store(database_.handle);
        sqlite3_progress_handler(database_.handle, kProgressInstructions, &QueryRunner::progress_trampoline, this);
        engine_detail::load_trips(
            database_.handle, trips_,
            [this](std::size_t done, std::size_t total) {
                events_.send(RunnerEvent{.kind = RunnerEvent::Kind::Loading, .done = done, .total = total});
            },
            [this] { return !stop_.stop_requested(); });
        if (stop.stop_requested()) return;
        events_.send(RunnerEvent{.kind = RunnerEvent::Kind::Ready});

        while (!stop.stop_requested()) {
            Request request;
            {
                std::unique_lock lock{request_mutex_};
                request_ready_.wait(lock, stop, [this] { return pending_.has_value(); });
                if (stop.stop_requested() || !pending_) break;
                request = std::move(*pending_);
                pending_.reset();
            }
            if (auto outcome = execute(request)) results_.set(std::move(*outcome));
        }
        database_handle_.store(nullptr);
    }

    // Decides what an interrupted statement means: cancelled by the user, replaced by a newer
    // request (dropped silently), or something else stopping it (reported as cancelled).
    std::optional<QueryOutcome> settle(QueryOutcome outcome) {
        if (cancel_requested_.exchange(false)) {
            outcome.cancelled = true;
            outcome.error.clear();
            outcome.result = {};
            return outcome;
        }
        if (!outcome.cancelled) return outcome;
        if (superseded()) return std::nullopt;
        outcome.result = {};
        return outcome;
    }

    std::optional<QueryOutcome> execute(const Request& request) {
        QueryOutcome outcome{.generation = request.generation, .sql = request.sql};
        query_started_ = now();
        last_publish_ = query_started_;
        in_query_ = true;
        auto read = engine_detail::read_all(database_.handle, request.sql);
        in_query_ = false;

        outcome.result = std::move(read.set);
        outcome.error = std::move(read.error);
        outcome.truncated = read.truncated;
        outcome.cancelled = read.interrupted;
        outcome.elapsed_s = std::chrono::duration<double>(now() - query_started_).count();
        return settle(std::move(outcome));
    }

    std::vector<Trip> trips_;
    std::stop_token stop_{};
    std::chrono::steady_clock::time_point query_started_{};
    std::chrono::steady_clock::time_point last_publish_{};
    bool in_query_ = false;
    std::atomic<bool> cancel_requested_{false};
    // Published so cancel() can interrupt from the logic thread without touching database_ while
    // the worker is opening or closing it.
    std::atomic<sqlite3*> database_handle_{nullptr};
    engine_detail::Database database_{};
    // The worker runs multi-second queries, so nothing here is real-time: a plain mutex buys the
    // "latest request wins" slot with no ownership rules to get wrong.
    std::mutex request_mutex_;
    std::condition_variable_any request_ready_;
    std::optional<Request> pending_;
    core::Shared<QueryOutcome> results_{};
    core::Channel<RunnerEvent> events_{};
    // Last, so the worker is joined before the state it reads is destroyed.
    std::jthread thread_;
};

} // namespace prism::data_studio
