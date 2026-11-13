// Perf-lab benchmark: what re-sorting a sorted table costs when a few rows change, the shape of
// a live feed (doc/review-2026-11-12.md P3 #10). A full re-sort rebuilds every row's key and
// sorts them; a table that knows its changed rows (TableState::changed_rows, kept by a table
// over a List) re-keys those and merges them back (ui::refresh_order).
//
//   ./builddir/benchmarks/bench_table_sort            # N = 10000, 100000, 1000000
//   ./builddir/benchmarks/bench_table_sort 200000     # one N
#include <prism/ui/table_ops.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

using namespace prism;

namespace {
double median_ms(std::vector<double> v) {
    std::ranges::sort(v);
    return v[v.size() / 2];
}

// Median time of refresh_order after `changed` rows change, the table told which rows
// (`patch`) or not.
double resort_ms(std::size_t n, std::size_t changed, bool patch) {
    std::vector<double> values(n);
    for (std::size_t i = 0; i < n; ++i) values[i] = static_cast<double>((i * 7919) % 100003) / 7.0;
    ui::TableState ts;
    ts.source = {
        .column_count = [] { return std::size_t{1}; },
        .row_count = [n] { return n; },
        .cell_text = [&values](std::size_t r, std::size_t) { return fmt::format("{:.2f}", values[r]); },
        .header = [](std::size_t) { return std::string_view{"value"}; },
    };
    ts.column_count = 1;
    ts.sort.set(ui::SortSpec{0, true});
    ui::refresh_order(ts);
    std::vector<double> times;
    for (std::size_t round = 0; round < 7; ++round) {
        for (std::size_t k = 0; k < changed; ++k) {
            auto r = (round * 7919 + k * 104729) % n;
            values[r] += 1.0;
            ts.changed_rows.push_back(r);
        }
        if (!patch) ts.order_stale = true;
        auto t0 = std::chrono::steady_clock::now();
        ui::refresh_order(ts);
        auto t1 = std::chrono::steady_clock::now();
        if (ts.order.size() != n) std::abort();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return median_ms(times);
}

void run(std::size_t n) {
    for (std::size_t changed : {std::size_t{1}, std::size_t{100}})
        fmt::print("{:>9} rows, {:>3} changed: full re-sort {:8.2f} ms, changed rows only {:7.3f} ms\n",
                   n, changed, resort_ms(n, changed, false), resort_ms(n, changed, true));
}
}

int main(int argc, char** argv) {
    if (argc > 1) run(std::strtoull(argv[1], nullptr, 10));
    else for (std::size_t n : {10'000u, 100'000u, 1'000'000u}) run(n);
}
