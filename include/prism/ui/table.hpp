#pragma once

#include <prism/core/field.hpp>
#include <prism/core/list.hpp>
#include <prism/core/traits.hpp>
#include <prism/core/types.hpp>
#include <prism/core/reflect_annotations.hpp>
#include <prism/core/reflect_leaf.hpp>
#include <prism/ui/text_edit.hpp>
#include <prism/ui/widget_node.hpp>

#include <fmt/format.h>
#include <cassert>
#include <charconv>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if __cpp_impl_reflection
#include <meta>
#endif

namespace prism::ui {
using namespace prism::core;


struct TableSource {
    std::function<size_t()> column_count;
    std::function<size_t()> row_count;
    std::function<std::string(size_t row, size_t col)> cell_text;
    std::function<std::string_view(size_t col)> header;
    // Takes the text the user typed into a cell; false refuses it (not a number, say), and the
    // editor stays open. Unset: the table is read-only.
    std::function<bool(size_t row, size_t col, std::string_view text)> set_cell{};
    // Which columns take edits; unset: all of them (when set_cell is set).
    std::function<bool(size_t col)> column_editable{};
    // A cell holding a checkbox (nullopt: a plain cell): it draws the box instead of its text,
    // and a click toggles it through set_cell, with "true" or "false".
    std::function<std::optional<bool>(size_t row, size_t col)> cell_check{};
    // An icon drawn before the cell's text (nullptr: none).
    std::function<std::shared_ptr<const Image>(size_t row, size_t col)> cell_image{};
};

template <typename T>
concept ColumnStorage = requires(const T& t, size_t r, size_t c) {
    { t.column_count() } -> std::convertible_to<size_t>;
    { t.row_count() } -> std::convertible_to<size_t>;
    { t.cell_text(r, c) } -> std::convertible_to<std::string>;
    { t.header(c) } -> std::convertible_to<std::string_view>;
};

// The source's set_cell, if it has one (bool set_cell(row, col, text)): its tables are editable.
template <ColumnStorage T>
std::function<bool(size_t, size_t, std::string_view)> column_set_cell(T& data) {
    if constexpr (requires(size_t r, std::string_view t) { { data.set_cell(r, r, t) } -> std::convertible_to<bool>; })
        return [d = &data](size_t r, size_t c, std::string_view t) { return static_cast<bool>(d->set_cell(r, c, t)); };
    else
        return {};
}

// The source's editable(col), if it has one: which columns take edits.
template <ColumnStorage T>
std::function<bool(size_t)> column_editable_of(T& data) {
    if constexpr (requires(size_t c) { { data.editable(c) } -> std::convertible_to<bool>; })
        return [d = &data](size_t c) { return static_cast<bool>(d->editable(c)); };
    else
        return {};
}

// The source's cell_check(row, col) and cell_image(row, col), if it has them.
template <ColumnStorage T>
std::function<std::optional<bool>(size_t, size_t)> column_cell_check(T& data) {
    if constexpr (requires(size_t r) { { data.cell_check(r, r) } -> std::convertible_to<std::optional<bool>>; })
        return [d = &data](size_t r, size_t c) -> std::optional<bool> { return d->cell_check(r, c); };
    else
        return {};
}

template <ColumnStorage T>
std::function<std::shared_ptr<const Image>(size_t, size_t)> column_cell_image(T& data) {
    if constexpr (requires(size_t r) { { data.cell_image(r, r) } -> std::convertible_to<std::shared_ptr<const Image>>; })
        return [d = &data](size_t r, size_t c) -> std::shared_ptr<const Image> { return d->cell_image(r, c); };
    else
        return {};
}

template <ColumnStorage T>
TableSource wrap_column_storage(T& data) {
    return TableSource{
        .column_count = [&data] { return data.column_count(); },
        .row_count = [&data] { return data.row_count(); },
        .cell_text = [&data](size_t r, size_t c) -> std::string {
            return std::string(data.cell_text(r, c));
        },
        .header = [&data](size_t c) -> std::string_view {
            return data.header(c);
        },
        .set_cell = column_set_cell(data),
        .column_editable = column_editable_of(data),
        .cell_check = column_cell_check(data),
        .cell_image = column_cell_image(data),
    };
}

// Intentionally minimal: only gates overload selection. The actual row-element
// constraints are enforced by wrap_row_storage (Task 3) via reflection.
template <typename T>
concept RowStorage = requires {
    requires is_list_v<std::remove_cvref_t<T>>;
};

#if __cpp_impl_reflection

namespace table_detail {
template <typename T>
std::string field_to_string(const Field<T>& f) {
    if constexpr (std::is_same_v<T, std::string>)
        return f.get();
    else if constexpr (std::is_arithmetic_v<T>)
        return fmt::to_string(f.get());
    else
        return "?";
}

// Reads a cell's text into a column's value: a string as is, a number or a bool parsed whole
// (surrounding spaces allowed). False, and `out` untouched, for text that is not one.
template <typename T>
bool parse_cell(std::string_view text, T& out) {
    auto first = text.find_first_not_of(' '), last = text.find_last_not_of(' ');
    std::string_view t = first == std::string_view::npos ? std::string_view{} : text.substr(first, last - first + 1);
    if constexpr (std::is_same_v<T, std::string>) {
        out = std::string(text);
        return true;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (t == "true" || t == "1" || t == "yes") { out = true; return true; }
        if (t == "false" || t == "0" || t == "no") { out = false; return true; }
        return false;
    } else if constexpr (std::is_arithmetic_v<T>) {
        T v{};
        auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
        if (t.empty() || ec != std::errc{} || p != t.data() + t.size()) return false;
        out = v;
        return true;
    } else {
        return false;   // simplify: other column types (enums, structs) are not editable yet
    }
}

template <typename Row>
consteval bool has_any_field_member() {
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));
    bool found = false;
    template for (constexpr auto m : members) {
        using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
        if constexpr (is_field_v<M>) found = true;
    }
    return found;
}
} // namespace table_detail

template <RowStorage L>
TableSource wrap_row_storage(L& list) {
    using Row = typename std::remove_cvref_t<L>::value_type;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));
    static constexpr bool any_field_member = table_detail::has_any_field_member<Row>();

    // Build header names at static-init time
    static const auto headers = [] {
        std::vector<std::string> h;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (any_field_member) {
                if constexpr (is_field_v<M>) {
                    h.emplace_back(std::meta::identifier_of(m));
                }
            } else if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                constexpr auto override_label = extract_string_annotation<m, label_t>();
                if constexpr (!override_label.empty())
                    h.emplace_back(override_label);
                else
                    h.emplace_back(std::meta::identifier_of(m));
            }
        }
        return h;
    }();

    return TableSource{
        .column_count = [&list] { return headers.size(); },
        .row_count = [&list] { return list.size(); },
        .cell_text = [&list](size_t row, size_t col) -> std::string {
            size_t idx = 0;
            std::string result;
            const auto& r = list[row];
            template for (constexpr auto m : members) {
                auto& member = r.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (any_field_member) {
                    if constexpr (is_field_v<M>) {
                        if (idx == col)
                            result = table_detail::field_to_string(member);
                        ++idx;
                    }
                } else if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                    if (idx == col)
                        result = format_leaf_value(member);
                    ++idx;
                }
            }
            return result;
        },
        .header = [](size_t col) -> std::string_view {
            return headers[col];
        },
        // Plain rows: copied, the column parsed into its member, set back (the List reports the
        // row as updated). simplify: rows of Field members stay read-only -- a Field cannot be
        // copied, and setting one in place would not tell the List the row changed.
        .set_cell = [l = &list] {
            std::function<bool(size_t, size_t, std::string_view)> set;
            if constexpr (!any_field_member && std::is_copy_constructible_v<Row>)
                set = [l](size_t row, size_t col, std::string_view text) {
                    auto& list = *l;
                    if (row >= list.size()) return false;
                    Row copy = list[row];
                    size_t idx = 0;
                    bool ok = false;
                    template for (constexpr auto m : members) {
                        using M = std::remove_cvref_t<decltype(copy.[:m:])>;
                        if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                            if (idx == col) ok = table_detail::parse_cell(text, copy.[:m:]);
                            ++idx;
                        }
                    }
                    if (ok) list.set(row, std::move(copy));
                    return ok;
                };
            return set;
        }(),
        // A bool column is a checkbox column.
        .cell_check = [&list](size_t row, size_t col) -> std::optional<bool> {
            size_t idx = 0;
            std::optional<bool> result;
            const auto& r = list[row];
            template for (constexpr auto m : members) {
                using M = std::remove_cvref_t<decltype(r.[:m:])>;
                if constexpr (any_field_member) {
                    if constexpr (is_field_v<M>) {
                        if constexpr (std::is_same_v<M, Field<bool>>)
                            if (idx == col) result = r.[:m:].get();
                        ++idx;
                    }
                } else if constexpr (!has_annotation<m, decltype(skip)>() && LeafType<M>) {
                    if constexpr (std::is_same_v<M, bool>)
                        if (idx == col) result = r.[:m:];
                    ++idx;
                }
            }
            return result;
        },
    };
}

namespace table_detail {
template <typename T>
struct IsLeafVector : std::false_type {};
template <typename X>
struct IsLeafVector<std::vector<X>> : std::bool_constant<LeafType<X>> {};

template <typename T>
consteval bool has_any_soa_column() {
    if constexpr (!std::is_class_v<T>) {
        return false;
    } else {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
        bool found = false;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (!has_annotation<m, decltype(skip)>() && IsLeafVector<M>::value) {
                found = true;
            }
        }
        return found;
    }
}
} // namespace table_detail

template <typename T>
inline constexpr bool is_soa_struct_v = table_detail::has_any_soa_column<std::remove_cvref_t<T>>();

// Order matters: !is_list_v<T> must be checked (and short-circuit) before
// is_soa_struct_v<T>, or a List<T> would have its private items_ member
// (a std::vector<T>) reflected via access_context::unchecked() and
// spuriously match IsLeafVector, misclassifying every List<LeafType> as
// SOA-shaped. Likewise !ColumnStorage<T> must precede is_soa_struct_v so a
// hand-written ColumnStorage type with vector members isn't reclassified.
template <typename T>
concept SoaStorage = !is_list_v<std::remove_cvref_t<T>>
                      && !ColumnStorage<T>
                      && is_soa_struct_v<T>;

template <SoaStorage T>
TableSource wrap_soa_columns(T& data) {
    using Row = std::remove_cvref_t<T>;
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Row, std::meta::access_context::unchecked()));

    static const auto headers = [] {
        std::vector<std::string> h;
        template for (constexpr auto m : members) {
            using M = std::remove_cvref_t<typename[:std::meta::type_of(m):]>;
            if constexpr (!has_annotation<m, decltype(skip)>() && table_detail::IsLeafVector<M>::value) {
                constexpr auto override_label = extract_string_annotation<m, label_t>();
                if constexpr (!override_label.empty())
                    h.emplace_back(override_label);
                else
                    h.emplace_back(std::meta::identifier_of(m));
            }
        }
        return h;
    }();

    return TableSource{
        .column_count = [] { return headers.size(); },
        .row_count = [&data] {
            size_t n = 0;
            bool found = false;
            template for (constexpr auto m : members) {
                auto& member = data.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (!has_annotation<m, decltype(skip)>() && table_detail::IsLeafVector<M>::value) {
                    if (!found) { n = member.size(); found = true; }
                    else assert(member.size() == n && "SOA columns must all be the same size");
                }
            }
            return n;
        },
        .cell_text = [&data](size_t row, size_t col) -> std::string {
            size_t idx = 0;
            std::string result;
            template for (constexpr auto m : members) {
                auto& member = data.[:m:];
                using M = std::remove_cvref_t<decltype(member)>;
                if constexpr (!has_annotation<m, decltype(skip)>() && table_detail::IsLeafVector<M>::value) {
                    if (idx == col)
                        result = format_leaf_value(member[row]);
                    ++idx;
                }
            }
            return result;
        },
        .header = [](size_t col) -> std::string_view {
            return headers[col];
        },
    };
}

#endif // __cpp_impl_reflection

struct SortKey {
    std::optional<double> number;  // leading number, e.g. 12 for "12 MB"
    std::string text;
};

struct SortSpec {
    size_t column = 0;
    bool descending = false;
    bool operator==(const SortSpec&) const = default;
};

// The cell being edited: the source row and column, and the text being typed (it starts as
// the cell's text, all selected). `rejected`: the source refused the last commit.
struct CellEdit {
    size_t row = 0;
    size_t column = 0;
    TextEdit text{};
    bool rejected = false;
};

struct ColumnResize {
    size_t column;
    float grab_dx;  // pointer offset from the column's right edge when grabbed
};

struct TableState {
    size_t column_count = 0;
    // The first frame showing rows moved by a data change or a re-sort: a body click hit-tested
    // against an earlier frame is dropped (see WidgetTree::input_frame).
    std::uint64_t shifted_at = 0;

    DY scroll_y{0};
    Height viewport_h{0};
    Height row_height{0};
    ItemIndex visible_start{0};
    ItemIndex visible_end{0};
    ItemCount overscan{2};

    ScrollEventPolicy event_policy{ScrollEventPolicy::BubbleAtBounds};
    uint8_t show_ticks = 0;

    DX scroll_x{0};
    Width viewport_w{0};
    Width total_columns_w{0};

    // Selection is in data rows (source indices), so it survives re-sorting.
    // selected_row is the current row; selected_rows every selected row, ascending.
    Field<std::optional<size_t>> selected_row{std::nullopt};
    Field<std::vector<size_t>> selected_rows{};
    std::optional<size_t> anchor_row;  // where shift-click / shift+arrow ranges start

    Field<std::optional<SortSpec>> sort{std::nullopt};
    // View position -> data row, and its inverse; both empty when unsorted.
    std::vector<size_t> order;
    std::vector<size_t> rank;
    std::optional<SortSpec> order_spec;  // the spec `order` was built with
    bool order_stale = true;             // set when the data changes in a way rows cannot tell
    // Re-sorting only what changed (ui::refresh_order): every row's sort key, kept, and the rows
    // updated or appended since. A table over a List<T> reports them (rows_tracked).
    std::vector<SortKey> keys;
    std::vector<size_t> changed_rows;
    bool rows_tracked = false;
    std::vector<Connection> row_watch;   // the List's per-row signals, for changed_rows

    std::optional<CellEdit> editing;   // a cell being edited in place (ui::begin_edit)

    std::vector<float> column_widths;  // user-resized widths; 0 or missing = auto
    float auto_column_w = 120.f;
    std::optional<ColumnResize> resizing;

    TableSource source;

    std::vector<WidgetNode> pool;

    std::vector<std::string> header_overrides;

    size_t row_count() const {
        return source.row_count ? source.row_count() : 0;
    }

    std::string_view column_header(size_t col) const {
        if (col < header_overrides.size() && !header_overrides[col].empty())
            return header_overrides[col];
        return source.header ? source.header(col) : "";
    }
};

} // namespace prism::ui
