#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

// What the studio offers to run and what it says the tables look like: the seven saved queries and
// the schema panel's two tables. Static data, next to the dataset it queries.
namespace prism::data_studio {

// The editor opens on the query that also has a chart, so the first frame is already useful and
// Ctrl+Enter does something sensible before the user has typed anything.
inline constexpr std::size_t kOpeningQuery = 0;

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
