#pragma once

#include <prism/ui/table.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism::ui {

inline constexpr float min_column_w = 24.f;
inline constexpr float resize_grip = 4.f;

// ---- Sorting ---------------------------------------------------------------

inline std::optional<double> leading_number(std::string_view s) {
    auto first = s.find_first_not_of(" \t");
    if (first == std::string_view::npos) return std::nullopt;
    if (s[first] == '+') ++first;  // from_chars rejects it; "+5" and "-5" must group together
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
namespace table_detail {
inline std::vector<SortKey> sort_keys(const TableSource& src, size_t column) {
    size_t n = src.row_count();
    std::vector<SortKey> keys;
    keys.reserve(n);
    for (size_t r = 0; r < n; ++r) keys.push_back(make_sort_key(src.cell_text(r, column)));
    return keys;
}

// Rows by key, equal keys in row order: a total order, so a full sort and a merge of the
// changed rows give the same result.
inline auto row_less(const std::vector<SortKey>& keys, bool descending) {
    return [&keys, descending](size_t a, size_t b) {
        int c = compare_keys(keys[a], keys[b]);
        if (c != 0) return descending ? c > 0 : c < 0;
        return a < b;
    };
}

inline std::vector<size_t> sorted_rows(const std::vector<SortKey>& keys, bool descending) {
    std::vector<size_t> order(keys.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::sort(order, row_less(keys, descending));
    return order;
}
} // namespace table_detail

inline std::vector<size_t> sort_order(const TableSource& src, SortSpec spec) {
    return table_detail::sorted_rows(table_detail::sort_keys(src, spec.column), spec.descending);
}

inline SortSpec next_sort(std::optional<SortSpec> current, size_t column) {
    if (current && current->column == column) return {column, !current->descending};
    return {column, false};
}

// Drops selected rows past the end of the data (rows were removed).
inline void prune_selection(TableState& ts) {
    size_t n = ts.row_count();
    auto& rows = ts.selected_rows.get();
    if (!rows.empty() && rows.back() >= n)
        ts.selected_rows.set(std::vector<size_t>(rows.begin(), std::ranges::lower_bound(rows, n)));
    if (ts.selected_row.get() >= n) ts.selected_row.set(std::nullopt);  // nullopt compares less
    if (ts.anchor_row >= n) ts.anchor_row.reset();
}

namespace table_detail {
// The rows updated or appended since the last sort (rows past the old order are the appended
// ones), each once.
inline std::vector<size_t> rows_to_resort(TableState& ts) {
    auto rows = std::move(ts.changed_rows);
    for (size_t r = ts.order.size(); r < ts.row_count(); ++r) rows.push_back(r);
    std::erase_if(rows, [n = ts.row_count()](size_t r) { return r >= n; });
    std::ranges::sort(rows);
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
    return rows;
}

// The order kept, the changed rows taken out, re-keyed, sorted and merged back in:
// O(n + k log k) for k changed rows, against O(n log n) and n keys built for a full sort.
inline void resort_rows(TableState& ts, const std::vector<size_t>& rows, SortSpec spec) {
    size_t n = ts.row_count();
    ts.keys.resize(n);
    std::vector<bool> moved(n);
    for (auto r : rows) {
        ts.keys[r] = make_sort_key(ts.source.cell_text(r, spec.column));
        moved[r] = true;
    }
    std::erase_if(ts.order, [&](size_t r) { return moved[r]; });
    auto sorted = rows;
    auto less = row_less(ts.keys, spec.descending);
    std::ranges::sort(sorted, less);
    std::vector<size_t> order;
    order.reserve(n);
    std::ranges::merge(ts.order, sorted, std::back_inserter(order), less);
    ts.order = std::move(order);
}

// Whether the kept order and keys can be patched: same sort, no change rows cannot tell
// (order_stale), no row removed, and few enough changed rows that merging beats sorting.
// simplify: a removal, an insert before the end, and any change to a column-storage table
// still re-sort everything (100k rows: ~20 ms). Patching those needs the source to say which
// rows moved where, as List does for updates and appends.
inline bool can_patch(const TableState& ts, SortSpec spec, size_t changed) {
    size_t n = ts.row_count();
    return !ts.order_stale && ts.order_spec == spec && ts.keys.size() == ts.order.size()
        && ts.order.size() <= n && changed <= n / 4;
}
} // namespace table_detail

inline void refresh_order(TableState& ts) {
    prune_selection(ts);
    auto spec = ts.sort.get();
    if (!spec || spec->column >= ts.column_count) {
        ts.order.clear();
        ts.rank.clear();
        ts.keys.clear();
        ts.changed_rows.clear();
        ts.order_spec.reset();
        return;
    }
    auto rows = table_detail::rows_to_resort(ts);
    if (!ts.order_stale && ts.order_spec == spec && rows.empty() && ts.order.size() == ts.row_count()) return;
    if (table_detail::can_patch(ts, *spec, rows.size())) {
        table_detail::resort_rows(ts, rows, *spec);
    } else {
        ts.keys = table_detail::sort_keys(ts.source, spec->column);
        ts.order = table_detail::sorted_rows(ts.keys, spec->descending);
    }
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
    bool removing = std::ranges::binary_search(rows, row);
    if (removing)
        rows.erase(std::ranges::lower_bound(rows, row));
    else
        rows.insert(std::ranges::lower_bound(rows, row), row);
    ts.selected_rows.set(std::move(rows));
    ts.selected_row.set(removing ? std::nullopt : std::optional{row});
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
    if (!ts.selected_row.get() && ts.row_count() > 0) {
        ts.selected_row.set(source_row(ts, 0));
        ts.anchor_row = source_row(ts, 0);
    }
}

// ---- Editing cells ----------------------------------------------------------

inline bool editable(const TableState& ts) { return static_cast<bool>(ts.source.set_cell); }

inline bool column_editable(const TableState& ts, size_t column) {
    return editable(ts) && column < ts.column_count && (!ts.source.column_editable || ts.source.column_editable(column));
}

inline bool is_check_cell(const TableState& ts, size_t row, size_t column) {
    return ts.source.cell_check && row < ts.row_count() && ts.source.cell_check(row, column).has_value();
}

// Whether the text editor may open on a cell: an editable column, not a checkbox.
inline bool text_editable(const TableState& ts, std::optional<size_t> row, size_t column) {
    return column_editable(ts, column) && !(row && is_check_cell(ts, *row, column));
}

// The column after (or, `step` -1, before) `from` whose cell in `row` takes text, if any.
inline std::optional<size_t> next_editable_column(const TableState& ts, size_t from, int step,
                                                  std::optional<size_t> row = std::nullopt) {
    for (auto c = static_cast<std::ptrdiff_t>(from) + step; c >= 0 && c < static_cast<std::ptrdiff_t>(ts.column_count); c += step)
        if (text_editable(ts, row, static_cast<size_t>(c))) return static_cast<size_t>(c);
    return std::nullopt;
}

inline std::optional<size_t> first_editable_column(const TableState& ts, std::optional<size_t> row = std::nullopt) {
    return text_editable(ts, row, 0) ? std::optional<size_t>{0} : next_editable_column(ts, 0, +1, row);
}

// Flips a checkbox cell through set_cell; false when the cell has no box, the column takes no
// edits, or the source refuses.
inline bool toggle_check(TableState& ts, size_t row, size_t column) {
    if (!column_editable(ts, column) || !is_check_cell(ts, row, column)) return false;
    return ts.source.set_cell(row, column, *ts.source.cell_check(row, column) ? "false" : "true");
}

// Scrolls sideways just enough to show column `c` (its left edge, if it is wider than the view).
inline void reveal_column(TableState& ts, size_t c) {
    auto edges = column_edges(ts);
    float view = ts.viewport_w.raw(), x = ts.scroll_x.raw();
    if (edges[c + 1] > x + view) x = edges[c + 1] - view;
    if (edges[c] < x) x = edges[c];
    ts.scroll_x = DX{std::max(0.f, x)};
}

// Opens the editor on a cell (source row, column), its text all selected, scrolled into view.
inline void begin_edit(TableState& ts, size_t row, size_t column) {
    if (!text_editable(ts, row, column) || row >= ts.row_count()) return;
    reveal_column(ts, column);
    ts.editing = CellEdit{row, column, text_edit::select_all(TextEdit{ts.source.cell_text(row, column)})};
}

inline void cancel_edit(TableState& ts) { ts.editing.reset(); }

// Hands the typed text to the source: true and closed if it took it, false and marked if not.
inline bool commit_edit(TableState& ts) {
    if (!ts.editing) return true;
    auto& e = *ts.editing;
    if (!ts.source.set_cell(e.row, e.column, e.text.text)) {
        e.rejected = true;
        return false;
    }
    ts.editing.reset();
    return true;
}

} // namespace prism::ui
