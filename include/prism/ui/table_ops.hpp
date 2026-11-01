#pragma once

#include <prism/ui/table.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism::ui {

inline constexpr float min_column_w = 24.f;
inline constexpr float resize_grip = 4.f;

// ---- Sorting ---------------------------------------------------------------

struct SortKey {
    std::optional<double> number;  // leading number, e.g. 12 for "12 MB"
    std::string text;
};

inline std::optional<double> leading_number(std::string_view s) {
    auto first = s.find_first_not_of(' ');
    if (first == std::string_view::npos) return std::nullopt;
    double v = 0;
    auto [ptr, ec] = std::from_chars(s.data() + first, s.data() + s.size(), v);
    if (ec != std::errc{} || std::isnan(v)) return std::nullopt;
    return v;
}

inline SortKey make_sort_key(std::string text) {
    auto n = leading_number(text);
    return SortKey{n, std::move(text)};
}

// Numbers compare by value and come before text; ties and text compare bytewise.
inline int compare_keys(const SortKey& a, const SortKey& b) {
    if (a.number && b.number && *a.number != *b.number)
        return *a.number < *b.number ? -1 : 1;
    if (a.number.has_value() != b.number.has_value())
        return a.number ? -1 : 1;
    return a.text.compare(b.text) < 0 ? -1 : (a.text == b.text ? 0 : 1);
}

inline int compare_cells(std::string_view a, std::string_view b) {
    return compare_keys(make_sort_key(std::string(a)), make_sort_key(std::string(b)));
}

// Stable, so equal cells keep data order in both directions.
inline std::vector<size_t> sort_order(const TableSource& src, SortSpec spec) {
    size_t n = src.row_count();
    std::vector<SortKey> keys;
    keys.reserve(n);
    for (size_t r = 0; r < n; ++r) keys.push_back(make_sort_key(src.cell_text(r, spec.column)));
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, [&](size_t a, size_t b) {
        int c = compare_keys(keys[a], keys[b]);
        return spec.descending ? c > 0 : c < 0;
    });
    return order;
}

inline SortSpec next_sort(std::optional<SortSpec> current, size_t column) {
    if (current && current->column == column) return {column, !current->descending};
    return {column, false};
}

// simplify: re-sorts the whole table on every data change (keys are rebuilt from
// cell_text). Fine to ~100k rows at a few updates/s; past that, re-sort only the
// changed rows (or sort in the model) and merge.
inline void refresh_order(TableState& ts) {
    auto spec = ts.sort.get();
    if (!spec || spec->column >= ts.column_count) {
        ts.order.clear();
        ts.rank.clear();
        ts.order_spec.reset();
        return;
    }
    if (!ts.order_stale && ts.order_spec == spec && ts.order.size() == ts.row_count()) return;
    ts.order = sort_order(ts.source, *spec);
    ts.rank.assign(ts.order.size(), 0);
    for (size_t pos = 0; pos < ts.order.size(); ++pos) ts.rank[ts.order[pos]] = pos;
    ts.order_spec = spec;
    ts.order_stale = false;
}

inline size_t source_row(const TableState& ts, size_t view_pos) {
    return view_pos < ts.order.size() ? ts.order[view_pos] : view_pos;
}

inline size_t view_position(const TableState& ts, size_t row) {
    return row < ts.rank.size() ? ts.rank[row] : row;
}

// ---- Columns ---------------------------------------------------------------

inline float column_width(const TableState& ts, size_t c) {
    if (c < ts.column_widths.size() && ts.column_widths[c] > 0) return ts.column_widths[c];
    return ts.auto_column_w;
}

// Left edge of every column plus the right edge of the last one (n + 1 entries).
inline std::vector<float> column_edges(const TableState& ts) {
    std::vector<float> edges{0.f};
    for (size_t c = 0; c < ts.column_count; ++c) edges.push_back(edges.back() + column_width(ts, c));
    return edges;
}

inline std::optional<size_t> column_at(const std::vector<float>& edges, float x) {
    auto it = std::ranges::upper_bound(edges, x);
    if (it == edges.begin() || it == edges.end()) return std::nullopt;
    return static_cast<size_t>(it - edges.begin() - 1);
}

// The column whose right edge is within the grip of x.
inline std::optional<size_t> resize_handle_at(const std::vector<float>& edges, float x) {
    for (size_t c = 1; c < edges.size(); ++c)
        if (std::abs(x - edges[c]) <= resize_grip) return c - 1;
    return std::nullopt;
}

inline void set_column_width(TableState& ts, size_t c, float w) {
    if (ts.column_widths.size() <= c) ts.column_widths.resize(c + 1, 0.f);
    ts.column_widths[c] = std::max(min_column_w, w);
}

inline float max_scroll_x(const TableState& ts) {
    return std::max(0.f, column_edges(ts).back() - ts.viewport_w.raw());
}

// ---- Selection -------------------------------------------------------------

inline bool is_selected(const TableState& ts, size_t row) {
    return ts.selected_row.get() == row || std::ranges::binary_search(ts.selected_rows.get(), row);
}

inline void clear_selection(TableState& ts) {
    ts.selected_row.set(std::nullopt);
    ts.selected_rows.set({});
    ts.anchor_row.reset();
}

inline void select_single(TableState& ts, size_t row) {
    ts.selected_rows.set({row});
    ts.selected_row.set(row);
    ts.anchor_row = row;
}

// Every row between the anchor and view_pos, in view order -- the rows the user sees.
inline void select_range_to(TableState& ts, size_t view_pos) {
    size_t from = view_position(ts, ts.anchor_row.value_or(source_row(ts, view_pos)));
    std::vector<size_t> rows;
    for (size_t p = std::min(from, view_pos); p <= std::max(from, view_pos); ++p)
        rows.push_back(source_row(ts, p));
    std::ranges::sort(rows);
    ts.selected_rows.set(std::move(rows));
    ts.selected_row.set(source_row(ts, view_pos));
}

inline void toggle_row(TableState& ts, size_t row) {
    auto rows = ts.selected_rows.get();
    if (auto it = std::ranges::lower_bound(rows, row); it != rows.end() && *it == row)
        rows.erase(it);
    else
        rows.insert(it, row);
    ts.selected_rows.set(std::move(rows));
    ts.selected_row.set(row);
    ts.anchor_row = row;
}

inline bool only_selected(const TableState& ts, size_t row) {
    auto& rows = ts.selected_rows.get();
    return ts.selected_row.get() == row && (rows.empty() || rows == std::vector<size_t>{row});
}

inline void select_all(TableState& ts) {
    std::vector<size_t> rows(ts.row_count());
    std::iota(rows.begin(), rows.end(), size_t{0});
    ts.selected_rows.set(std::move(rows));
}

} // namespace prism::ui
