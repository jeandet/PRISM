#include <prism/prism.hpp>
#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>
#include <prism/delegates/form_delegates.hpp>
#include <prism/ui/dock.hpp>
#include <prism/ui/shortcut.hpp>
#include <prism/widgets/plot.hpp>

#include "query_runner.hpp"
#include "studio_model.hpp"

#include "../showcase/showcase_common.hpp"
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace prism;
using namespace prism::data_studio;

namespace {

// The editor opens on the query that also has a chart, so the first frame is already useful and
// Ctrl+Enter does something sensible before the user has typed anything.
constexpr std::size_t kOpeningQuery = 0;
constexpr std::size_t kEditorRows = 8;

} // namespace

// Glaze reflects this on save/load, so it cannot live in the anonymous namespace.
struct DataStudioPrefs {
    core::Field<ui::DockLayout> layout{{ui::dock_split(
        /*vertical=*/false,
        {ui::dock_split(true, {ui::dock_group({"schema"}), ui::dock_group({"queries"})}, {0.46f, 0.54f}),
         ui::dock_split(true, {ui::dock_group({"editor"}), ui::dock_group({"results"})}, {0.46f, 0.54f})},
        {0.24f, 0.76f})}};
    app::WindowGeometry window;
};

namespace {

enum class StatusKind : std::uint8_t { Idle, Loading, Running, Cancelling, Done, Error, Cancelled };

struct Status {
    StatusKind kind = StatusKind::Loading;
    std::string text = "building the trips table...";
    double progress = 0.0;   // Loading: 0..1 done
    bool operator==(const Status&) const = default;
};

namespace data_studio_detail {

inline core::Point at(float x, float y) { return core::Point{core::X{x}, core::Y{y}}; }

inline bool working(StatusKind kind) {
    return kind == StatusKind::Loading || kind == StatusKind::Running || kind == StatusKind::Cancelling;
}

// Errors have to read as errors in a line of plain text: word them, don't just colour them.
inline std::string status_line(const Status& status) {
    return status.kind == StatusKind::Error ? "Error: " + status.text : status.text;
}

// Loading knows its fraction; a running query does not, and SQLite will not pretend it does.
// Idle is not busy either: a `busy` bar asks for a frame every 33 ms, which is not something to
// do while nothing is running.
inline ui::ProgressBar progress_bar(const Status& status) {
    if (!working(status.kind)) return {.value = 0.0, .min = 0.0, .max = 1.0};
    if (status.kind == StatusKind::Loading) return {.value = status.progress, .min = 0.0, .max = 1.0};
    return {.busy = true};
}

inline std::optional<double> column_number(const std::vector<Cell>& row, std::size_t column) {
    return column < row.size() ? cell_number(row[column]) : std::nullopt;
}

} // namespace data_studio_detail

struct DataStudioApp;

// One canvas per model object, so each drawing job is its own small type holding the model.
struct SchemaView {
    DataStudioApp* app = nullptr;
    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node);
};

struct DataStudioApp {
    DataStudioPrefs prefs{};

    core::Field<ui::TextArea<>> editor{
        {.value = std::string{kSavedQueries[kOpeningQuery].sql}, .rows = kEditorRows}};
    core::Field<ui::Button> run_button{{.text = "Run",
                                        .primary = true,
                                        .shortcut = ui::parse_shortcut("Ctrl+Enter").value_or(ui::Shortcut{})}};
    core::Field<ui::Button> cancel_button{{.text = "Cancel", .primary = false}};
    core::Field<ui::Label<>> run_hint{{.value = "Ctrl+Enter runs the editor's query"}};
    core::Field<ui::TabBar<>> results_tab{};
    core::Field<std::optional<std::size_t>> selected_query{};
    core::List<std::string> query_list{};
    // Wrapped: a saved query's note is a sentence, and the panel is narrower than one.
    core::Derived<ui::Label<>> query_note{
        [this] {
            auto index = selected_query.get();
            auto note = index && *index < kSavedQueries.size() ? std::string{kSavedQueries[*index].note}
                                                               : std::string{};
            return ui::Label<>{.value = std::move(note), .wrap = true};
        },
        selected_query};

    plot::PlotModel chart{};

    // The table and the chart read from here. Nothing in it is a Field, so the tree never has to
    // track it: a result arriving bumps `results_revision`, which the table depends on.
    struct Ui {
        ResultSet results{};
        core::Field<std::uint32_t> results_revision{0};
        core::Field<Status> status{};
        core::Field<std::uint32_t> loaded_rows{0};
        std::vector<std::string> chart_labels{};
    } ui;

    core::Derived<std::string> status_text{[this] { return data_studio_detail::status_line(ui.status.get()); },
                                           ui.status};
    core::Derived<ui::ProgressBar> progress{[this] { return data_studio_detail::progress_bar(ui.status.get()); },
                                            ui.status};

    SchemaView schema_view{this};

    // Created by start_runner() on the interactive path only: the SVG capture runs its query
    // synchronously and never needs a worker.
    std::unique_ptr<QueryRunner> runner{};
    std::uint64_t generation_ = 0;
    bool marking_error_ = false;
    bool auto_ran_ = false;
    std::vector<core::Connection> wiring{};

    DataStudioApp() {
        for (auto& query : kSavedQueries) query_list.push_back(std::string{query.name});
        wiring.push_back(run_button.on_change().connect([this](const auto&) { submit(editor.get().value); }));
        wiring.push_back(cancel_button.on_change().connect([this](const auto&) { cancel(); }));
        wiring.push_back(editor.on_change().connect([this](const auto&) { editor_changed(); }));
    }

    // Generates the dataset and starts the worker that loads it.
    void start_runner(std::function<void()> waker) {
        runner = std::make_unique<QueryRunner>(make_trips(kFullDatasetRows));
        runner->events().set_waker(waker);
        runner->events().observe([this](const RunnerEvent& event) { on_runner_event(event); });
        runner->results().set_waker(std::move(waker));
        runner->results().observe([this](const QueryOutcome& outcome) { on_outcome(outcome); });
    }

    // Called by the widget tree once per frame, after the model's notifications are drained.
    void drain() {
        if (runner) runner->drain();
    }

    void submit(std::string sql) {
        if (!runner) return;
        if (sql.find_first_not_of(" \t\r\n") == std::string::npos) {
            set_status(StatusKind::Idle, "nothing to run");
            return;
        }
        ++generation_;
        clear_error_mark();
        set_status(StatusKind::Running, running_status(0.0));
        runner->request(std::move(sql), generation_);
    }

    void cancel() {
        if (!runner || !data_studio_detail::working(ui.status.get().kind)) return;
        runner->cancel();
        set_status(StatusKind::Cancelling, "cancelling...");
    }

    // --- results -------------------------------------------------------------------------

    void on_outcome(const QueryOutcome& outcome) {
        if (outcome.generation != generation_) return;   // an answer to a query already replaced
        if (!outcome.error.empty()) return show_error(outcome);
        if (outcome.cancelled) return set_status(StatusKind::Cancelled, cancelled_status(outcome.elapsed_s));
        show_result(outcome);
    }

    void show_result(const QueryOutcome& outcome) {
        ui.results = outcome.result;
        ui.results_revision.set(ui.results_revision.get() + 1);
        update_chart(ui.results);
        auto text = done_status(ui.results.row_count(), outcome.elapsed_s);
        if (outcome.truncated) text += fmt::format(" (the first {})", rows_text(kMaxResultRows));
        set_status(StatusKind::Done, std::move(text));
    }

    // A bad query leaves the last good result on screen, the way any SQL tool does: only the
    // status line and the mark in the editor change.
    void show_error(const QueryOutcome& outcome) {
        set_status(StatusKind::Error, outcome.error);
        mark_error(outcome.sql, outcome.error);
    }

    void update_chart(const ResultSet& set) {
        chart.clear_series();
        ui.chart_labels.clear();
        chart.x_tick_format = nullptr;
        auto plan = chart_plan(set);
        if (plan.plottable) {
            chart.y_label.set(std::string{set.header(plan.y_column)});
            if (plan.numeric_x) chart_numeric(set, plan);
            else chart_categorical(set, plan);
        }
        chart.notify();
    }

    void chart_numeric(const ResultSet& set, const ChartPlan& plan) {
        chart.x_label.set(std::string{set.header(plan.x_column)});
        std::vector<double> xs;
        std::vector<double> ys;
        for (auto& row : set.rows) {
            auto x = data_studio_detail::column_number(row, plan.x_column);
            auto y = data_studio_detail::column_number(row, plan.y_column);
            if (!x || !y) continue;
            xs.push_back(*x);
            ys.push_back(*y);
        }
        chart.add_series(std::move(xs), std::move(ys), plot::SeriesStyle{.color = series_color()});
    }

    // A text column cannot be an x value, so it becomes the tick labels and rows become the axis.
    void chart_categorical(const ResultSet& set, const ChartPlan& plan) {
        chart.x_label.set("");
        std::vector<double> ys;
        for (auto& row : set.rows) {
            auto y = data_studio_detail::column_number(row, plan.y_column);
            if (!y || plan.x_column >= row.size()) continue;
            ys.push_back(*y);
            ui.chart_labels.push_back(cell_text(row[plan.x_column]));
        }
        std::vector<double> xs(ys.size());
        for (std::size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<double>(i);
        chart.x_tick_format = [this](double value) { return category_tick(ui.chart_labels, value); };
        chart.add_series(std::move(xs), std::move(ys),
                         plot::SeriesStyle{.color = series_color(), .thickness = 2.f, .fill = true});
    }

    static core::Color series_color() { return plot::default_series_colors(ui::default_theme())[0]; }

    // --- editor marks --------------------------------------------------------------------

    void mark_error(const std::string& sql, const std::string& message) {
        auto range = error_mark(sql, message);
        if (!range) return;
        auto area = editor.get();
        area.highlights = {ui::TextRange{range->begin, range->end}};
        marking_error_ = true;
        editor.set(std::move(area));
        marking_error_ = false;
    }

    void clear_error_mark() {
        auto area = editor.get();
        if (area.highlights.empty()) return;
        area.highlights.clear();
        marking_error_ = true;
        editor.set(std::move(area));
        marking_error_ = false;
    }

    // The user typing in the editor clears the underline the last error left there.
    void editor_changed() {
        if (marking_error_) return;
        clear_error_mark();
    }

    // The status line and the progress bar both read `ui.status`; neither knows about the other.
    void set_status(StatusKind kind, std::string text) {
        ui.status.set({.kind = kind, .text = std::move(text)});
    }

    // The loading line carries a fraction as well as words, so it cannot go through set_status.
    void set_loading(std::size_t done, std::size_t total) {
        auto progress = total ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
        ui.loaded_rows.set(static_cast<std::uint32_t>(done));
        ui.status.set({.kind = StatusKind::Loading,
                       .text = fmt::format("loading {} trips - {}%",
                                           with_thousands(static_cast<std::int64_t>(total)),
                                           std::lround(progress * 100.0)),
                       .progress = progress});
    }

    // --- runner events -------------------------------------------------------------------

    void on_runner_event(const RunnerEvent& event) {
        switch (event.kind) {
            case RunnerEvent::Kind::Loading:
                set_loading(event.done, event.total);
                break;
            case RunnerEvent::Kind::Ready:
                if (!auto_ran_) {   // the editor opens on a query: show its result unasked
                    auto_ran_ = true;
                    submit(editor.get().value);
                    break;
                }
                set_status(StatusKind::Idle,
                           fmt::format("{} trips ready - Ctrl+Enter runs the query",
                                       with_thousands(static_cast<std::int64_t>(ui.loaded_rows.get()))));
                break;
            case RunnerEvent::Kind::Running:
                if (ui.status.get().kind == StatusKind::Running)
                    set_status(StatusKind::Running, running_status(event.elapsed));
                break;
        }
    }

    void open_query(std::size_t index) {
        if (index >= kSavedQueries.size()) return;
        auto area = editor.get();
        area.value = std::string{kSavedQueries[index].sql};
        area.highlights.clear();
        editor.set(std::move(area));
    }

    // --- the view ------------------------------------------------------------------------

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(prefs.layout,
                    {{"schema", "Schema", [&](app::WidgetTree::ViewBuilder& panel) { view_schema(panel); }},
                     {"queries", "Saved queries", [&](app::WidgetTree::ViewBuilder& panel) { view_queries(panel); }},
                     {"editor", "Query", [&](app::WidgetTree::ViewBuilder& panel) { view_editor(panel); }},
                     {"results", "Results", [&](app::WidgetTree::ViewBuilder& panel) { view_results(panel); }}});
            vb.status_bar([&] { view_status(vb); });
        });
    }

    // Plain widgets only: a canvas in the status bar takes whatever it is given, which is the
    // whole bar. The bar stays in the tree and reports itself idle, so the layout never jumps.
    void view_status(app::WidgetTree::ViewBuilder& vb) {
        vb.widget(status_text);
        vb.spacer();
        vb.widget(progress);
    }

    void view_schema(app::WidgetTree::ViewBuilder& vb) { vb.canvas(schema_view).depends_on(ui.loaded_rows); }

    void view_queries(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.list<std::string>(query_list, [&](std::size_t index, const std::string&) { open_query(index); })
                .selection(selected_query);
            vb.gap(6.f);
            vb.widget(query_note);
        });
    }

    void view_editor(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.toolbar([&] {
                vb.widget(run_button);
                vb.widget(cancel_button);
                vb.spacer();
                vb.widget(run_hint);
            });
            vb.widget(editor);
        });
    }

    // tabs() takes a zero-argument builder: the pages are declared with tab() on the builder it
    // is filling, and only the selected page is ever built.
    void view_results(app::WidgetTree::ViewBuilder& vb) {
        vb.tabs(results_tab, [&] {
            vb.tab("Table", [&](app::WidgetTree::ViewBuilder& page) {
                page.table(ui.results).depends_on(ui.results_revision);
            });
            vb.tab("Chart", [&](app::WidgetTree::ViewBuilder& page) {
                page.canvas(chart)
                    .depends_on(chart.x_range, chart.y_range, chart.view, chart.cursor, chart.revision);
            });
        });
    }

    // --- the SVG capture -----------------------------------------------------------------

    void seed_demo_data() {
        auto trips = make_trips(kCaptureRows);
        auto outcome = run_synchronously(trips, kSavedQueries[kOpeningQuery].sql);
        ui.loaded_rows.set(static_cast<std::uint32_t>(kCaptureRows));
        ui.results = std::move(outcome.result);
        ui.results_revision.set(1);
        update_chart(ui.results);
        selected_query.set(kOpeningQuery);
        set_status(StatusKind::Done, done_status(ui.results.row_count(), outcome.elapsed_s));
    }
};

void SchemaView::canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
    if (!app) return;
    const auto& theme = ui::node_theme(node);
    constexpr float kIndent = 12.f;
    constexpr float kTypeColumn = 132.f;
    constexpr float kRowHeight = 15.f;
    auto x = bounds.origin.x.raw() + 12.f;
    auto y = bounds.origin.y.raw() + 10.f;
    auto rows = app->ui.loaded_rows.get();

    dl.clip_push(bounds.origin, bounds.extent);
    for (auto& table : kSchema) {
        dl.text(std::string{table.name}, data_studio_detail::at(x, y), 13.f, theme.text);
        if (table.name == "trips" && rows > 0)
            dl.text(rows_text(rows), data_studio_detail::at(x + kTypeColumn, y + 1.f), 12.f, theme.accent);
        y += kRowHeight + 4.f;
        for (auto& column : table.columns) {
            dl.text(std::string{column.name}, data_studio_detail::at(x + kIndent, y), 11.5f, theme.text_muted, 0.f,
                    render::TextAnchor::TopLeft, render::FontRole::Mono);
            dl.text(std::string{column.type}, data_studio_detail::at(x + kTypeColumn, y), 11.5f, theme.text_placeholder,
                    0.f, render::TextAnchor::TopLeft, render::FontRole::Mono);
            y += kRowHeight;
        }
        y += 8.f;
    }
    dl.clip_pop();
}

} // namespace

int main(int argc, char* argv[]) {
    DataStudioApp app;
    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1180, 760);
    }

    app::SettingsFile settings{app.prefs, backends::settings_dir("prism", "data-studio") / "settings.toml"};
    app::model_app(app.prefs.window.applied_to({.title = "PRISM Data Studio",
                                                .width = 1180,
                                                .height = 760,
                                                .decoration = app::DecorationMode::Custom}),
                   app, [&app](app::AppContext& ctx) {
                       ctx.remember_window_size(app.prefs.window);
                       // The busy progress bar asks for a frame every 33 ms through node.redraw_at;
                       // a running clock is what turns those requests into frames.
                       ctx.clock().add([](auto) { return true; });
                       app.start_runner(ctx.waker());
                   });
    return 0;
}
