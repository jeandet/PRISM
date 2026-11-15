#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What a query gives back, in the shape the table and the chart read: typed cells, the set itself,
// and the rule that decides which two columns are worth plotting. No PRISM, no SQLite.
namespace prism::data_studio {

// A bigger result set than this is cut: `SELECT * FROM trips` would otherwise put 500k rows of
// formatted strings in memory just to scroll past them.
inline constexpr std::size_t kMaxResultRows = 100'000;

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

// The number in one cell of a row, for the chart: a short row (a NULL column dropped by SQLite)
// has no value at that index.
inline std::optional<double> row_number(const std::vector<Cell>& row, std::size_t column) {
    return column < row.size() ? cell_number(row[column]) : std::nullopt;
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

} // namespace prism::data_studio
