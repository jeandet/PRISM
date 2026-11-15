#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// The generated year of taxi trips, with no SQLite, no PRISM and no threads in it: the zone table
// the generator draws from, and the trips it makes. tests/test_data_studio.cpp checks the
// distributions and the formatting without opening a database.
namespace prism::data_studio {

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

} // namespace prism::data_studio
