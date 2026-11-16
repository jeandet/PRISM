#pragma once

#include <prism/prism.hpp>
#include <prism/widgets/plot.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../sql/result_set.hpp"

namespace prism::data_studio::blocks {

// The results panel: the table and the chart, both reading the one result set the app hands over.
// Neither is a widget built over the rows: the table is a ColumnStorage view of `set`, and a
// revision Field is what its depends_on watches.
struct Results {
    // Nested so the unplaced-field check, which only looks at this struct's own members, has
    // nothing to say about a revision that the table places.
    struct Source {
        ResultSet set{};
        core::Field<std::uint32_t> revision{0};
    } source{};

    core::Field<ui::TabBar<>> tabs{};
    plot::PlotModel chart{};
    std::vector<std::string> labels{};   // one per plotted row, when the x axis is categorical

    void show(ResultSet result) {
        source.set = std::move(result);
        source.revision.set(source.revision.get() + 1);
        update_chart();
    }

    std::size_t rows() const { return source.set.row_count(); }

    void view(app::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Table", [&](app::ViewBuilder& page) {
                page.table(source.set).depends_on(source.revision);
            });
            vb.tab("Chart", [&](app::ViewBuilder& page) {
                page.canvas(chart)
                    .depends_on(chart.x_range, chart.y_range, chart.view, chart.cursor, chart.revision);
            });
        });
    }

private:
    void update_chart() {
        chart.clear_series();
        labels.clear();
        chart.x_tick_format = nullptr;
        auto plan = chart_plan(source.set);
        if (plan.plottable) {
            chart.y_label.set(std::string{source.set.header(plan.y_column)});
            if (plan.numeric_x) chart_numeric(plan);
            else chart_categorical(plan);
        }
        chart.notify();
    }

    void chart_numeric(const ChartPlan& plan) {
        chart.x_label.set(std::string{source.set.header(plan.x_column)});
        std::vector<double> xs;
        std::vector<double> ys;
        for (auto& row : source.set.rows) {
            auto x = row_number(row, plan.x_column);
            auto y = row_number(row, plan.y_column);
            if (!x || !y) continue;
            xs.push_back(*x);
            ys.push_back(*y);
        }
        chart.add_series(std::move(xs), std::move(ys), plot::SeriesStyle{.color = series_color()});
    }

    // A text column cannot be an x value, so it becomes the tick labels and rows become the axis.
    void chart_categorical(const ChartPlan& plan) {
        chart.x_label.set("");
        std::vector<double> ys;
        for (auto& row : source.set.rows) {
            auto y = row_number(row, plan.y_column);
            if (!y || plan.x_column >= row.size()) continue;
            ys.push_back(*y);
            labels.push_back(cell_text(row[plan.x_column]));
        }
        std::vector<double> xs(ys.size());
        for (std::size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        chart.x_tick_format = [this](double value) { return category_tick(labels, value); };
        chart.add_series(std::move(xs), std::move(ys),
                         plot::SeriesStyle{.color = series_color(), .thickness = 2.f, .fill = true});
    }

    static core::Color series_color() { return plot::default_series_colors(ui::default_theme())[0]; }
};

} // namespace prism::data_studio::blocks
