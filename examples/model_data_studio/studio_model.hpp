#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the data studio shows, without SQLite, threads or a window: the generated trip table, the
// result set the table and the chart read, the saved queries, and the status-line wording. Pure
// on purpose -- tests/test_data_studio.cpp checks all of it without a database or a backend.

namespace prism::data_studio {

// ---------------------------------------------------------------------------------------------
// The generated dataset
// ---------------------------------------------------------------------------------------------

struct Zone {
    std::string_view name;
    std::string_view borough;
    std::uint8_t weight;   // how much of the city's traffic starts here
};

// Roughly the shape of a real zone table: a handful of Manhattan zones carry most of the trips,
// the outer boroughs a few each.
inline constexpr std::array<Zone, 18> kZones{{
    {"Midtown", "Manhattan", 18},
    {"Upper East Side", "Manhattan", 12},
    {"Upper West Side", "Manhattan", 11},
    {"Chelsea", "Manhattan", 10},
    {"East Village", "Manhattan", 9},
    {"Financial District", "Manhattan", 8},
    {"Harlem", "Manhattan", 6},
    {"Williamsburg", "Brooklyn", 9},
    {"Park Slope", "Brooklyn", 7},
    {"Bushwick", "Brooklyn", 6},
    {"Bedford-Stuyvesant", "Brooklyn", 6},
    {"Coney Island", "Brooklyn", 3},
    {"Astoria", "Queens", 7},
    {"Flushing", "Queens", 6},
    {"Long Island City", "Queens", 5},
    {"Jamaica", "Queens", 4},
    {"Riverdale", "Bronx", 3},
    {"St. George", "Staten Island", 2},
}};

inline constexpr std::array<std::string_view, 4> kPayments{"card", "cash", "app", "account"};

inline constexpr std::int32_t kMinutesInYear = 365 * 24 * 60;

// Demand by hour of day: the two commute peaks, and the thin 4 a.m. trough.
inline constexpr std::array<std::uint8_t, 24> kHourWeight{
    14, 9, 6, 4, 3, 4, 8, 16, 26, 24, 16, 15, 17, 16, 16, 18, 24, 29, 26, 21, 24, 27, 24, 19};

// kHourWeight's largest value after the weekend night boost (29 -> 48).
inline constexpr std::uint8_t kPeakHourWeight = 48;

// 500k trips is what the desktop window loads: big enough that the slow query takes seconds and
// scrolling the table is a real test, small enough to build in about a second.
inline constexpr std::size_t kFullDatasetRows = 500'000;
// The SVG capture runs one query with no threads, so it seeds a smaller year.
inline constexpr std::size_t kCaptureRows = 4'000;

// One row of the trips table, as the generator makes it. Minutes-into-the-year keeps the hour
// arithmetic integral; the timestamp text is derived when the row is inserted.
struct Trip {
    std::int32_t pickup_minute = 0;
    std::uint8_t zone = 0;        // index into kZones
    std::uint8_t payment = 0;     // index into kPayments
    std::uint8_t passengers = 1;
    double km = 0.0;
    double minutes = 0.0;
    double fare = 0.0;
    double tip = 0.0;
    bool operator==(const Trip&) const = default;
};

// splitmix64: the same trip table on every standard library, unlike <random>'s distributions,
// whose output is implementation-defined. Determinism is tested, so it has to hold everywhere.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform in [0, 1).
    double unit() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

    std::size_t below(std::size_t bound) { return bound == 0 ? 0 : next() % bound; }

    // Box-Muller; the caller clamps its result, so the far tail costs nothing here.
    double normal() {
        auto u = std::max(unit(), 1e-12);
        return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * unit());
    }

private:
    std::uint64_t state_;
};

inline bool is_weekend(std::int32_t minute) {
    // 1 January is a Monday in the generated year, so day 5 is Saturday.
    return (minute / (24 * 60) + 1) % 7 >= 5;
}

inline std::uint8_t hour_weight(std::int32_t minute) {
    auto hour = static_cast<std::size_t>((minute / 60) % 24);
    auto weight = kHourWeight[hour];
    if (is_weekend(minute) && (hour < 5 || hour >= 22)) weight = static_cast<std::uint8_t>(weight * 2);
    return weight;
}

// Rejection sampling against the hour weights: uniform minutes, kept in proportion to demand.
inline std::int32_t pickup_minute(Rng& rng) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        auto minute = static_cast<std::int32_t>(rng.below(static_cast<std::size_t>(kMinutesInYear)));
        if (rng.below(kPeakHourWeight) < hour_weight(minute)) return minute;
    }
    return 12 * 60;
}

inline std::uint8_t pick_zone(Rng& rng) {
    unsigned total = 0;
    for (auto& zone : kZones) total += zone.weight;
    auto target = static_cast<unsigned>(rng.below(total));
    unsigned running = 0;
    for (std::size_t i = 0; i < kZones.size(); ++i) {
        running += kZones[i].weight;
        if (target < running) return static_cast<std::uint8_t>(i);
    }
    return 0;
}

inline bool in_rush(std::int32_t minute) {
    auto hour = (minute / 60) % 24;
    return (hour >= 7 && hour <= 9) || (hour >= 16 && hour <= 19);
}

// Tip as a share of the fare: an app payment tips best, a card does the usual, an account never
// tips and a cash tip is whatever was left over.
inline double pick_tip_rate(Rng& rng, std::uint8_t payment) {
    switch (payment) {
        case 0: return 0.15 + 0.07 * rng.unit();
        case 1: return 0.10 * rng.unit();
        case 2: return 0.18 + 0.08 * rng.unit();
        default: return 0.0;
    }
}

inline Trip make_trip(Rng& rng) {
    Trip trip;
    trip.pickup_minute = pickup_minute(rng);
    trip.zone = pick_zone(rng);
    trip.payment = static_cast<std::uint8_t>(rng.below(kPayments.size()));
    trip.passengers = static_cast<std::uint8_t>(1 + rng.below(rng.below(4) == 0 ? 4 : 2));
    trip.km = std::clamp(std::exp(0.9 + 0.55 * rng.normal()), 0.4, 38.0);
    auto kmh = in_rush(trip.pickup_minute) ? 11.0 + 4.0 * rng.unit() : 18.0 + 10.0 * rng.unit();
    trip.minutes = trip.km / kmh * 60.0 + 1.5 + 2.5 * rng.unit();
    trip.fare = 3.0 + 2.6 * trip.km + 0.42 * trip.minutes;
    trip.tip = trip.fare * pick_tip_rate(rng, trip.payment);
    return trip;
}

inline std::vector<Trip> make_trips(std::size_t count, std::uint64_t seed = 0x5150) {
    Rng rng{seed};
    std::vector<Trip> trips;
    trips.reserve(count);
    for (std::size_t i = 0; i < count; ++i) trips.push_back(make_trip(rng));
    return trips;
}

// "2025-03-14 08:41:00": what the trips.pickup TEXT column holds, so SQLite's date functions work.
inline std::string pickup_text(std::int32_t minute) {
    static constexpr std::array<std::uint8_t, 12> month_days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    auto day = minute / (24 * 60);
    auto minute_of_day = minute % (24 * 60);
    std::size_t month = 0;
    while (day >= month_days[month]) day -= month_days[month++];
    return std::format("2025-{:02}-{:02} {:02}:{:02}:00", month + 1, day + 1, minute_of_day / 60,
                       minute_of_day % 60);
}

// ---------------------------------------------------------------------------------------------
// Result sets: what a query gives back, in the shape the table and the chart read
// ---------------------------------------------------------------------------------------------

struct Cell {
    enum class Kind : std::uint8_t { Null, Integer, Real, Text };

    Kind kind = Kind::Null;
    std::int64_t integer = 0;
    double real = 0.0;
    std::string text{};

    bool operator==(const Cell&) const = default;
};

inline Cell null_cell() { return Cell{}; }

inline Cell int_cell(std::int64_t value) {
    Cell cell;
    cell.kind = Cell::Kind::Integer;
    cell.integer = value;
    return cell;
}

inline Cell real_cell(double value) {
    Cell cell;
    cell.kind = Cell::Kind::Real;
    cell.real = value;
    return cell;
}

inline Cell text_cell(std::string value) {
    Cell cell;
    cell.kind = Cell::Kind::Text;
    cell.text = std::move(value);
    return cell;
}

inline std::optional<double> cell_number(const Cell& cell) {
    if (cell.kind == Cell::Kind::Integer) return static_cast<double>(cell.integer);
    if (cell.kind == Cell::Kind::Real) return cell.real;
    return std::nullopt;
}

// 1234567 -> "1,234,567"
inline std::string with_thousands(std::int64_t value) {
    auto digits = std::to_string(std::abs(value));
    for (auto at = digits.size(); at > 3; at -= 3) digits.insert(at - 3, 1, ',');
    return value < 0 ? "-" + digits : digits;
}

// Wide values lose their decimals, narrow ones keep two: a fare is readable either way, and a
// column of percentages stops looking ragged.
inline std::string format_real(double value) {
    if (!std::isfinite(value)) return "nan";
    auto magnitude = std::abs(value);
    if (magnitude >= 1000.0) return with_thousands(std::llround(value));
    if (magnitude >= 10.0) return std::format("{:.1f}", value);
    return std::format("{:.2f}", value);
}

inline std::string cell_text(const Cell& cell) {
    switch (cell.kind) {
        case Cell::Kind::Null: return "NULL";
        case Cell::Kind::Integer: return with_thousands(cell.integer);
        case Cell::Kind::Real: return format_real(cell.real);
        case Cell::Kind::Text: return cell.text;
    }
    return {};
}

// A table source (ui::ColumnStorage): header() and cell_text() are called on the logic thread
// while the table lays out and paints, so they must be const and cheap.
struct ResultSet {
    std::vector<std::string> columns{};
    std::vector<std::vector<Cell>> rows{};

    std::size_t column_count() const { return columns.size(); }
    std::size_t row_count() const { return rows.size(); }

    std::string cell_text(std::size_t row, std::size_t col) const {
        if (row >= rows.size() || col >= rows[row].size()) return {};
        return data_studio::cell_text(rows[row][col]);
    }

    std::string_view header(std::size_t col) const {
        return col < columns.size() ? std::string_view{columns[col]} : std::string_view{};
    }

    bool operator==(const ResultSet&) const = default;
};

// Which columns the chart should use: a text column labels the categories, two numeric columns
// make an x/y plot. Empty when the result has nothing worth drawing (a single COUNT, say).
struct ChartPlan {
    bool plottable = false;
    bool numeric_x = false;
    std::size_t x_column = 0;
    std::size_t y_column = 1;
    bool operator==(const ChartPlan&) const = default;
};

inline bool mostly_numeric(const ResultSet& set, std::size_t col) {
    std::size_t seen = 0;
    std::size_t numbers = 0;
    for (auto& row : set.rows) {
        if (col >= row.size() || row[col].kind == Cell::Kind::Null) continue;
        ++seen;
        if (cell_number(row[col])) ++numbers;
    }
    return seen >= 2 && numbers * 10 >= seen * 9;
}

inline std::size_t distinct_texts(const ResultSet& set, std::size_t col) {
    std::vector<std::string_view> seen;
    for (auto& row : set.rows) {
        if (col >= row.size() || row[col].kind != Cell::Kind::Text) continue;
        if (std::ranges::find(seen, row[col].text) == seen.end()) seen.push_back(row[col].text);
        if (seen.size() >= 3) break;
    }
    return seen.size();
}

inline ChartPlan chart_plan(const ResultSet& set) {
    ChartPlan plan;
    if (set.row_count() < 2 || set.column_count() < 2) return plan;

    std::size_t first_numeric = set.column_count();
    for (std::size_t col = 0; col < set.column_count(); ++col) {
        if (mostly_numeric(set, col)) {
            first_numeric = col;
            break;
        }
    }
    if (first_numeric == set.column_count()) return plan;

    for (std::size_t col = 0; col < first_numeric; ++col) {
        if (distinct_texts(set, col) >= 2)
            return ChartPlan{.plottable = true, .numeric_x = false, .x_column = col, .y_column = first_numeric};
    }
    for (std::size_t col = first_numeric + 1; col < set.column_count(); ++col) {
        if (mostly_numeric(set, col))
            return ChartPlan{.plottable = true, .numeric_x = true, .x_column = first_numeric, .y_column = col};
    }
    return plan;
}

// The plot picks its own tick positions, so on a categorical axis they land between rows as
// often as on one. Only a tick that sits on a whole row gets a label: rounding the nearest
// row's name instead draws "Brooklyn, Brooklyn, Staten Island, Staten Island".
inline std::string category_tick(const std::vector<std::string>& labels, double value) {
    auto row = std::llround(value);
    if (std::abs(value - static_cast<double>(row)) > 0.001) return {};
    if (row < 0 || static_cast<std::size_t>(row) >= labels.size()) return {};
    return labels[static_cast<std::size_t>(row)];
}

// ---------------------------------------------------------------------------------------------
// Status line wording
// ---------------------------------------------------------------------------------------------

inline std::string elapsed_text(double seconds) {
    if (seconds < 1.0) return std::format("{} ms", std::llround(seconds * 1000.0));
    return std::format("{:.1f} s", seconds);
}

inline std::string rows_text(std::size_t rows) {
    return std::format("{} {}", with_thousands(static_cast<std::int64_t>(rows)), rows == 1 ? "row" : "rows");
}

inline std::string done_status(std::size_t rows, double seconds) {
    return std::format("{} in {}", rows_text(rows), elapsed_text(seconds));
}

inline std::string running_status(double seconds) { return std::format("running {}...", elapsed_text(seconds)); }

inline std::string cancelled_status(double seconds) { return std::format("cancelled after {}", elapsed_text(seconds)); }

// ---------------------------------------------------------------------------------------------
// Editor error marks
// ---------------------------------------------------------------------------------------------

struct ByteRange {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool operator==(const ByteRange&) const = default;
};

// SQLite names what it choked on -- near "FROMM": syntax error, no such column: kmm -- so the
// editor can underline that token instead of only printing the message.
inline std::optional<ByteRange> error_mark(std::string_view sql, std::string_view message) {
    auto locate = [&](std::string_view token) -> std::optional<ByteRange> {
        if (token.empty()) return std::nullopt;
        auto at = sql.find(token);
        if (at == std::string_view::npos) return std::nullopt;
        return ByteRange{at, at + token.size()};
    };

    if (auto at = message.find("near \""); at != std::string_view::npos) {
        auto begin = at + 6;
        auto end = message.find('"', begin);
        if (end != std::string_view::npos) return locate(message.substr(begin, end - begin));
    }
    for (auto marker : {"no such column: ", "no such table: "}) {
        if (auto at = message.find(marker); at != std::string_view::npos) {
            auto rest = message.substr(at + std::string_view{marker}.size());
            return locate(rest.substr(0, rest.find_first_of(" ,")));
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// The saved queries and the schema the panels show
// ---------------------------------------------------------------------------------------------

struct SavedQuery {
    std::string_view name;
    std::string_view sql;
    std::string_view note;
};

inline constexpr std::array<SavedQuery, 7> kSavedQueries{{
    {"Busiest hours",
     R"(SELECT strftime('%H:00', pickup) AS hour, COUNT(*) AS trips
  FROM trips
 GROUP BY hour
 ORDER BY hour)",
     "demand by hour of day"},
    {"Tips by payment",
     R"(SELECT payment, ROUND(AVG(tip) * 100.0 / AVG(fare), 1) AS tip_pct, COUNT(*) AS trips
  FROM trips
 GROUP BY payment
 ORDER BY tip_pct DESC)",
     "who tips best"},
    {"Fare per km by borough",
     R"(SELECT z.borough, ROUND(SUM(t.fare) / SUM(t.km), 2) AS fare_per_km, COUNT(*) AS trips
  FROM trips AS t JOIN zones AS z ON z.id = t.zone_id
 GROUP BY z.borough
 ORDER BY fare_per_km DESC)",
     "a join, and an aggregate of an aggregate"},
    {"Distance vs duration",
     R"(SELECT ROUND(km) AS km, ROUND(AVG(minutes), 1) AS minutes, COUNT(*) AS trips
  FROM trips
 WHERE km < 25
 GROUP BY ROUND(km)
 ORDER BY km)",
     "two numbers: plotted as a line"},
    {"Slowest zones",
     R"(SELECT z.name AS zone, z.borough, ROUND(AVG(t.minutes), 1) AS avg_minutes, COUNT(*) AS trips
  FROM trips AS t JOIN zones AS z ON z.id = t.zone_id
 GROUP BY z.id
 ORDER BY avg_minutes DESC
 LIMIT 12)",
     "the twelve slowest zones, out of every trip"},
    {"Passengers per trip",
     R"(SELECT passengers, COUNT(*) AS trips, ROUND(AVG(fare), 2) AS avg_fare
  FROM trips
 GROUP BY passengers
 ORDER BY passengers)",
     "one row per party size"},
    {"Rush-hour fare premium (slow)",
     R"(SELECT z.borough,
       COUNT(*) AS pairs,
       ROUND(AVG(b.fare - a.fare), 2) AS fare_gap
  FROM (SELECT * FROM trips WHERE fare > 40 LIMIT 700) AS a
  JOIN trips AS b ON b.zone_id = a.zone_id
                 AND (b.pickup_minute % 1440) BETWEEN 420 AND 600
  JOIN zones AS z ON z.id = a.zone_id
 GROUP BY z.borough
 ORDER BY fare_gap DESC)",
     "deliberately slow: a self-join with no index to help it"},
}};

struct SchemaColumn {
    std::string_view name;
    std::string_view type;
};

struct SchemaTable {
    std::string_view name;
    std::vector<SchemaColumn> columns;
};

inline const std::vector<SchemaTable> kSchema = {
    {"trips",
     {{"id", "INTEGER"},
      {"pickup", "TEXT"},
      {"pickup_minute", "INTEGER"},
      {"zone_id", "INTEGER"},
      {"km", "REAL"},
      {"minutes", "REAL"},
      {"fare", "REAL"},
      {"tip", "REAL"},
      {"passengers", "INTEGER"},
      {"payment", "TEXT"}}},
    {"zones", {{"id", "INTEGER"}, {"name", "TEXT"}, {"borough", "TEXT"}}},
};

inline const std::vector<SchemaTable>& schema() { return kSchema; }

// The DDL the worker runs before it inserts the generated rows.
inline constexpr std::string_view kSchemaSql = R"(
CREATE TABLE zones (id INTEGER PRIMARY KEY, name TEXT NOT NULL, borough TEXT NOT NULL);
CREATE TABLE trips (
    id INTEGER PRIMARY KEY,
    pickup TEXT NOT NULL,
    pickup_minute INTEGER NOT NULL,
    zone_id INTEGER NOT NULL REFERENCES zones(id),
    km REAL NOT NULL,
    minutes REAL NOT NULL,
    fare REAL NOT NULL,
    tip REAL NOT NULL,
    passengers INTEGER NOT NULL,
    payment TEXT NOT NULL
);
)";

} // namespace prism::data_studio
