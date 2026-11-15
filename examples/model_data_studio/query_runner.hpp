#pragma once

#include "studio_model.hpp"

#include <prism/core/channel.hpp>
#include <prism/core/shared.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// The SQLite side of the data studio. Everything that touches SQLite lives in runner_detail and
// runs on whichever thread calls it; QueryRunner is the thin layer that puts it on a worker and
// brings results back over a Shared<T> and a Channel<T> for the model's drain() to pick up. The
// headless capture path calls runner_detail directly, so it needs no worker at all.

namespace prism::data_studio {

// A bigger result set than this is cut: `SELECT * FROM trips` would otherwise put 500k rows of
// formatted strings in memory just to scroll past them.
inline constexpr std::size_t kMaxResultRows = 100'000;
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

struct QueryOutcome {
    std::uint64_t generation = 0;   // the request this answers, so a stale one can be ignored
    std::string sql{};
    ResultSet result{};
    std::string error{};   // SQLite's message; empty on success
    bool cancelled = false;
    bool truncated = false;
    double elapsed_s = 0.0;
    bool operator==(const QueryOutcome&) const = default;
};

namespace runner_detail {

// Finalizes a statement on every exit path, including the early returns an error takes.
struct Statement {
    sqlite3_stmt* handle = nullptr;

    Statement() = default;
    explicit Statement(sqlite3_stmt* h) : handle(h) {}
    ~Statement() {
        if (handle) sqlite3_finalize(handle);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

// An in-memory database and the table definition, closed when it goes out of scope.
class Database {
public:
    Database() = default;
    ~Database() {
        if (handle) sqlite3_close(handle);
    }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    Database& operator=(Database&& other) noexcept {
        std::swap(handle, other.handle);
        return *this;
    }

    static Database open_memory() {
        Database database;
        if (sqlite3_open(":memory:", &database.handle) != SQLITE_OK) return Database{};
        database.exec("PRAGMA journal_mode = OFF");
        database.exec("PRAGMA synchronous = OFF");
        return database;
    }

    // Failures are ignored: on an in-memory database the only way these fail is running out of
    // memory, and the first query then reports it with SQLite's own message.
    void exec(std::string_view sql) {
        if (!handle) return;
        sqlite3_exec(handle, std::string{sql}.c_str(), nullptr, nullptr, nullptr);
    }

    sqlite3* handle = nullptr;
};

using LoadProgress = std::function<void(std::size_t done, std::size_t total)>;
using KeepGoing = std::function<bool()>;

inline void insert_zones(sqlite3* db) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, "INSERT INTO zones(id, name, borough) VALUES(?, ?, ?)", -1, &raw, nullptr) != SQLITE_OK)
        return;
    auto statement = Statement{raw};
    for (std::size_t i = 0; i < kZones.size(); ++i) {
        sqlite3_bind_int64(raw, 1, static_cast<sqlite3_int64>(i + 1));
        sqlite3_bind_text(raw, 2, kZones[i].name.data(), static_cast<int>(kZones[i].name.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(raw, 3, kZones[i].borough.data(), static_cast<int>(kZones[i].borough.size()),
                          SQLITE_TRANSIENT);
        sqlite3_step(raw);
        sqlite3_reset(raw);
    }
}

inline void insert_trips(sqlite3* db, const std::vector<Trip>& trips, const LoadProgress& progress,
                         const KeepGoing& keep_going) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO trips(id, pickup, pickup_minute, zone_id, km, minutes, fare, tip, "
                           "passengers, payment) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                           -1, &raw, nullptr) != SQLITE_OK)
        return;
    auto statement = Statement{raw};
    auto publish_every = std::max<std::size_t>(1, trips.size() / 24);
    for (std::size_t i = 0; i < trips.size(); ++i) {
        if (keep_going && !keep_going()) return;
        auto& trip = trips[i];
        auto pickup = pickup_text(trip.pickup_minute);
        sqlite3_bind_int64(raw, 1, static_cast<sqlite3_int64>(i + 1));
        sqlite3_bind_text(raw, 2, pickup.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(raw, 3, trip.pickup_minute);
        sqlite3_bind_int64(raw, 4, static_cast<sqlite3_int64>(trip.zone) + 1);
        sqlite3_bind_double(raw, 5, trip.km);
        sqlite3_bind_double(raw, 6, trip.minutes);
        sqlite3_bind_double(raw, 7, trip.fare);
        sqlite3_bind_double(raw, 8, trip.tip);
        sqlite3_bind_int64(raw, 9, trip.passengers);
        sqlite3_bind_text(raw, 10, kPayments[trip.payment].data(), static_cast<int>(kPayments[trip.payment].size()),
                          SQLITE_TRANSIENT);
        sqlite3_step(raw);
        sqlite3_reset(raw);
        if (progress && (i + 1) % publish_every == 0) progress(i + 1, trips.size());
    }
    if (progress) progress(trips.size(), trips.size());
}

inline void create_schema(sqlite3* db) {
    if (!db) return;
    sqlite3_exec(db, std::string{kSchemaSql}.c_str(), nullptr, nullptr, nullptr);
}

inline void load_trips(sqlite3* db, const std::vector<Trip>& trips, const LoadProgress& progress = {},
                       const KeepGoing& keep_going = {}) {
    if (!db) return;
    create_schema(db);
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    insert_zones(db);
    insert_trips(db, trips, progress, keep_going);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
}

struct ReadResult {
    ResultSet set{};
    std::string error{};
    bool truncated = false;
    // The progress handler said stop, or someone called sqlite3_interrupt: not an error.
    bool interrupted = false;
};

inline std::vector<Cell> read_row(sqlite3_stmt* statement, int columns) {
    std::vector<Cell> row;
    row.reserve(static_cast<std::size_t>(columns));
    for (int col = 0; col < columns; ++col) {
        switch (sqlite3_column_type(statement, col)) {
            case SQLITE_INTEGER: row.push_back(int_cell(sqlite3_column_int64(statement, col))); break;
            case SQLITE_FLOAT: row.push_back(real_cell(sqlite3_column_double(statement, col))); break;
            case SQLITE_NULL: row.push_back(null_cell()); break;
            default: {
                auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, col));
                row.push_back(text_cell(text ? std::string{text} : std::string{}));
            }
        }
    }
    return row;
}

inline bool is_interrupt(int status) { return status == SQLITE_INTERRUPT || status == SQLITE_ABORT; }

// Prepares and steps a statement into a ResultSet, capped at max_rows. Stopping early is detected
// from SQLITE_INTERRUPT, so the caller keeps control of when to abort (see QueryRunner).
inline ReadResult read_all(sqlite3* db, std::string_view sql, std::size_t max_rows = kMaxResultRows) {
    ReadResult read;
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, std::string{sql}.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
        read.error = sqlite3_errmsg(db);
        return read;
    }
    auto statement = Statement{raw};
    auto columns = sqlite3_column_count(raw);
    read.set.columns.reserve(static_cast<std::size_t>(columns));
    for (int col = 0; col < columns; ++col) read.set.columns.emplace_back(sqlite3_column_name(raw, col));

    while (read.set.rows.size() < max_rows) {
        auto status = sqlite3_step(raw);
        if (status == SQLITE_DONE) return read;
        if (is_interrupt(status)) {
            read.interrupted = true;
            read.set = {};
            return read;
        }
        if (status != SQLITE_ROW) {
            read.error = sqlite3_errmsg(db);
            return read;
        }
        read.set.rows.push_back(read_row(raw, columns));
    }
    read.truncated = sqlite3_step(raw) == SQLITE_ROW;
    return read;
}

} // namespace runner_detail

// One query with no thread at all: what the SVG capture and the tests use.
inline QueryOutcome run_synchronously(const std::vector<Trip>& trips, std::string_view sql) {
    auto database = runner_detail::Database::open_memory();
    if (!database.handle) return QueryOutcome{.sql = std::string{sql}, .error = "could not open an in-memory database"};
    runner_detail::load_trips(database.handle, trips);
    auto read = runner_detail::read_all(database.handle, sql);
    QueryOutcome outcome{.sql = std::string{sql}};
    outcome.result = std::move(read.set);
    outcome.error = std::move(read.error);
    outcome.truncated = read.truncated;
    outcome.cancelled = read.interrupted;
    return outcome;
}

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

    // Drained by the model's drain(), on the logic thread.
    core::Shared<QueryOutcome>& results() { return results_; }
    core::Channel<RunnerEvent>& events() { return events_; }
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
        database_ = runner_detail::Database::open_memory();
        if (!database_.handle) return;
        database_handle_.store(database_.handle);
        sqlite3_progress_handler(database_.handle, kProgressInstructions, &QueryRunner::progress_trampoline, this);
        runner_detail::load_trips(
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
        auto read = runner_detail::read_all(database_.handle, request.sql);
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
    runner_detail::Database database_{};
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
