#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../examples/model_data_studio/query_runner.hpp"
#include "../examples/model_data_studio/sql/catalog.hpp"
#include "../examples/model_data_studio/sql/dataset.hpp"
#include "../examples/model_data_studio/sql/errors.hpp"
#include "../examples/model_data_studio/sql/result_set.hpp"
#include "../examples/model_data_studio/sql/status.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace prism::data_studio;

namespace {

using steady = std::chrono::steady_clock;

// A recursive CTE that never finishes on its own: the only way out is the progress handler.
constexpr std::string_view kRunawaySql =
    "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 1000000000) SELECT count(*) FROM c";

// The runner publishes through a Shared and a Channel, which whoever owns the logic thread drains
// once a frame; a test has to play that role, so it polls drain() until an answer shows up.
std::optional<QueryOutcome> await_outcome(QueryRunner& runner, std::chrono::milliseconds timeout) {
    std::optional<QueryOutcome> outcome;
    runner.results().observe([&outcome](const QueryOutcome& result) { outcome = result; });
    auto deadline = steady::now() + timeout;
    while (steady::now() < deadline) {
        runner.drain();
        if (outcome) return outcome;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return std::nullopt;
}

ResultSet result_set(std::vector<std::string> columns, std::vector<std::vector<Cell>> rows) {
    return ResultSet{.columns = std::move(columns), .rows = std::move(rows)};
}

} // namespace

TEST_CASE("The generator gives the same year for the same seed") {
    // The dataset is regenerated on every run and on every machine, so the numbers can be quoted
    // in the README and compared between runs.
    CHECK(make_trips(500) == make_trips(500));
    CHECK(make_trips(500, 7) != make_trips(500));
}

TEST_CASE("Generated trips stay inside the ranges a taxi trip has") {
    for (auto& trip : make_trips(2000)) {
        CHECK(trip.pickup_minute >= 0);
        CHECK(trip.pickup_minute < kMinutesInYear);
        CHECK(trip.zone < kZones.size());
        CHECK(trip.payment < kPayments.size());
        CHECK(trip.passengers >= 1);
        CHECK(trip.passengers <= 4);
        CHECK(trip.km >= 0.4);
        CHECK(trip.km <= 38.0);
        CHECK(trip.minutes > 0.0);
        CHECK(trip.fare > 3.0);
        CHECK(trip.tip >= 0.0);
        CHECK(trip.tip < trip.fare);
    }
}

TEST_CASE("Pickups favour the commute over the small hours") {
    auto trips = make_trips(4000);
    auto rush = std::count_if(trips.begin(), trips.end(), [](const Trip& trip) { return in_rush(trip.pickup_minute); });
    // Rush hours are 7/24 of the clock but carry a bigger share of the weight table.
    CHECK(static_cast<double>(rush) / static_cast<double>(trips.size()) > 0.32);
}

TEST_CASE("Timestamps come out in a shape SQLite can read back") {
    CHECK(pickup_text(0) == "2025-01-01 00:00:00");
    CHECK(pickup_text(59) == "2025-01-01 00:59:00");
    CHECK(pickup_text(365 * 24 * 60 - 1) == "2025-12-31 23:59:00");
}

TEST_CASE("Cells read the way a person writes numbers") {
    CHECK(cell_text(int_cell(1234567)) == "1,234,567");
    CHECK(cell_text(int_cell(-2048)) == "-2,048");
    CHECK(cell_text(int_cell(24)) == "24");
    CHECK(cell_text(real_cell(4.5)) == "4.50");
    CHECK(cell_text(real_cell(12.345)) == "12.3");
    CHECK(cell_text(real_cell(1234.4)) == "1,234");
    CHECK(cell_text(text_cell("card")) == "card");
    CHECK(cell_text(null_cell()) == "NULL");
}

TEST_CASE("A text column labels the chart, a second number plots it") {
    auto categorical = result_set({"borough", "trips"},
                                  {{text_cell("Bronx"), int_cell(10)}, {text_cell("Queens"), int_cell(30)}});
    auto plan = chart_plan(categorical);
    CHECK(plan.plottable);
    CHECK_FALSE(plan.numeric_x);
    CHECK(plan.x_column == 0);
    CHECK(plan.y_column == 1);

    auto paired = result_set({"km", "minutes"}, {{real_cell(2.0), real_cell(11.0)}, {real_cell(9.0), real_cell(28.0)}});
    plan = chart_plan(paired);
    CHECK(plan.plottable);
    CHECK(plan.numeric_x);
    CHECK(plan.x_column == 0);
    CHECK(plan.y_column == 1);
}

TEST_CASE("A result with nothing to plot says so instead of drawing an empty box") {
    CHECK_FALSE(chart_plan(result_set({"trips"}, {{int_cell(500000)}})).plottable);       // single number
    CHECK_FALSE(chart_plan(result_set({"a", "b"}, {{text_cell("x"), int_cell(1)}})).plottable);   // one row
    CHECK_FALSE(chart_plan(ResultSet{}).plottable);
}

TEST_CASE("A categorical axis labels only the ticks that land on a row") {
    const std::vector<std::string> labels{"card", "cash", "app"};
    CHECK(category_tick(labels, 0.0) == "card");
    CHECK(category_tick(labels, 2.0) == "app");
    CHECK(category_tick(labels, 1.5).empty());   // between two rows: no label, not the nearest one
    CHECK(category_tick(labels, 3.0).empty());   // past the last row
    CHECK(category_tick(labels, -1.0).empty());
    CHECK(category_tick(std::vector<std::string>{}, 0.0).empty());
}

TEST_CASE("An error underlines the token SQLite complained about") {
    std::string_view sql = "SELECT * FROMM trips";
    auto marked = error_mark(sql, "near \"FROMM\": syntax error");
    REQUIRE(marked.has_value());
    CHECK(sql.substr(marked->begin, marked->end - marked->begin) == "FROMM");

    std::string_view typo = "SELECT kmm FROM trips";
    auto column = error_mark(typo, "no such column: kmm");
    REQUIRE(column.has_value());
    CHECK(typo.substr(column->begin, column->end - column->begin) == "kmm");

    CHECK_FALSE(error_mark("SELECT 1", "unable to open database file").has_value());
}

TEST_CASE("The status line reads like a sentence") {
    CHECK(done_status(1234, 0.087) == "1,234 rows in 87 ms");
    CHECK(done_status(1, 2.5) == "1 row in 2.5 s");
    CHECK(running_status(2.34) == "running 2.3 s...");
    CHECK(cancelled_status(0.4) == "cancelled after 400 ms");
}

TEST_CASE("A query against the generated year answers with rows and columns") {
    auto outcome = run_synchronously(make_trips(2000), "SELECT count(*) AS trips, max(fare) AS top FROM trips");
    CHECK(outcome.error.empty());
    CHECK(outcome.result.column_count() == 2);
    CHECK(outcome.result.row_count() == 1);
    CHECK(outcome.result.header(0) == "trips");
    CHECK(outcome.result.cell_text(0, 0) == "2,000");
}

TEST_CASE("A saved query runs through the same path the SVG capture uses") {
    // The capture path has no worker at all, so it must produce a full result synchronously.
    auto outcome = run_synchronously(make_trips(kCaptureRows), kSavedQueries[0].sql);
    CHECK(outcome.error.empty());
    CHECK_FALSE(outcome.cancelled);
    CHECK(outcome.result.row_count() == 24);   // one bucket per hour of the day
    CHECK(chart_plan(outcome.result).plottable);
}

TEST_CASE("The worker loads the dataset, then answers a query") {
    QueryRunner runner{make_trips(3000)};
    runner.request("SELECT count(*) FROM trips", 1);
    auto outcome = await_outcome(runner, std::chrono::milliseconds{10000});
    REQUIRE(outcome.has_value());
    CHECK(outcome->error.empty());
    CHECK(outcome->generation == 1);
    CHECK(outcome->result.row_count() == 1);
    CHECK(outcome->result.cell_text(0, 0) == "3,000");
}

TEST_CASE("A bad query comes back as the message SQLite produced") {
    QueryRunner runner{make_trips(200)};
    runner.request("SELECT nope FROM trips", 1);
    auto outcome = await_outcome(runner, std::chrono::milliseconds{10000});
    REQUIRE(outcome.has_value());
    CHECK(outcome->error.find("no such column") != std::string::npos);
    CHECK(outcome->result.row_count() == 0);
    CHECK_FALSE(outcome->cancelled);
}

TEST_CASE("Cancel stops a runaway query instead of waiting for it") {
    QueryRunner runner{make_trips(200)};
    runner.request(std::string{kRunawaySql}, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds{100});   // let the query actually start
    auto started = steady::now();
    runner.cancel();
    auto outcome = await_outcome(runner, std::chrono::milliseconds{5000});
    auto stopped_after = steady::now() - started;

    REQUIRE(outcome.has_value());
    CHECK(outcome->cancelled);
    CHECK(outcome->result.row_count() == 0);
    // sqlite3_interrupt takes effect at the next progress callback, which is microseconds of work.
    CHECK(stopped_after < std::chrono::milliseconds{1000});
}

TEST_CASE("A result wider than the table can hold reports the truncation") {
    QueryRunner runner{make_trips(10)};
    runner.request("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 150000) "
                   "SELECT x FROM c",
                   1);
    auto outcome = await_outcome(runner, std::chrono::milliseconds{10000});
    REQUIRE(outcome.has_value());
    CHECK(outcome->error.empty());
    CHECK(outcome->truncated);
    CHECK(outcome->result.row_count() == kMaxResultRows);
}
