#pragma once

#include <sqlite3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "catalog.hpp"
#include "dataset.hpp"
#include "result_set.hpp"

// The SQLite side of the studio. Everything that touches SQLite is here, in engine_detail, and runs
// on whichever thread calls it -- no worker, no PRISM types, no locks. `run_synchronously` is the
// whole engine in one call, which is what the SVG capture and the tests use; the interactive path
// puts the same calls on a thread (query_runner.hpp) and reads the progress callbacks.
namespace prism::data_studio {

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

namespace engine_detail {

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

} // namespace engine_detail

// One query with no thread at all: what the SVG capture and the tests use.
inline QueryOutcome run_synchronously(const std::vector<Trip>& trips, std::string_view sql) {
    auto database = engine_detail::Database::open_memory();
    if (!database.handle) return QueryOutcome{.sql = std::string{sql}, .error = "could not open an in-memory database"};
    engine_detail::load_trips(database.handle, trips);
    auto read = engine_detail::read_all(database.handle, sql);
    QueryOutcome outcome{.sql = std::string{sql}};
    outcome.result = std::move(read.set);
    outcome.error = std::move(read.error);
    outcome.truncated = read.truncated;
    outcome.cancelled = read.interrupted;
    return outcome;
}

} // namespace prism::data_studio
