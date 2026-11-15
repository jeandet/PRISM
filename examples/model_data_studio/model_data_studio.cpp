#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>

#include "../showcase/showcase_common.hpp"
#include <fmt/format.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "blocks/editor.hpp"
#include "blocks/queries.hpp"
#include "blocks/results.hpp"
#include "blocks/schema.hpp"
#include "blocks/status_bar.hpp"
#include "settings.hpp"
#include "query_runner.hpp"
#include "sql/catalog.hpp"
#include "sql/dataset.hpp"
#include "sql/status.hpp"

using namespace prism;
using namespace prism::data_studio;

// The app is composition only: four panels, one status bar, and the worker that answers a query.
// The SQLite side is sql/, how each panel looks is blocks/, and this file says what goes where.
struct DataStudioApp {
    DataStudioSettings settings{};

    blocks::StatusBar status{};
    blocks::Editor editor{};
    // A saved query goes into the editor when its row is clicked: this is the app's call to make,
    // since neither panel knows about the other.
    blocks::Queries queries{[this](std::size_t index) { editor.open(std::string{kSavedQueries[index].sql}); }};
    blocks::Results results{};
    blocks::Schema schema{status.rows()};

    // Created by start_runner() on the interactive path only: the SVG capture runs its query
    // synchronously and never needs a worker.
    std::unique_ptr<QueryRunner> runner{};
    std::uint64_t generation_ = 0;
    bool auto_ran_ = false;
    std::vector<core::Connection> wiring{};

    DataStudioApp() {
        wiring.push_back(editor.run.on_change().connect([this](const auto&) { submit(editor.sql()); }));
        wiring.push_back(editor.cancel.on_change().connect([this](const auto&) { cancel(); }));
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
            status.set(StatusKind::Idle, "nothing to run");
            return;
        }
        ++generation_;
        editor.clear_mark();
        status.set(StatusKind::Running, running_status(0.0));
        runner->request(std::move(sql), generation_);
    }

    void cancel() {
        if (!runner || !status.busy()) return;
        runner->cancel();
        status.set(StatusKind::Cancelling, "cancelling...");
    }

    void on_outcome(const QueryOutcome& outcome) {
        if (outcome.generation != generation_) return;   // an answer to a query already replaced
        if (!outcome.error.empty()) return show_error(outcome);
        if (outcome.cancelled) return status.set(StatusKind::Cancelled, cancelled_status(outcome.elapsed_s));
        show_result(outcome);
    }

    void show_result(const QueryOutcome& outcome) {
        results.show(outcome.result);
        auto text = done_status(results.rows(), outcome.elapsed_s);
        if (outcome.truncated) text += fmt::format(" (the first {})", rows_text(kMaxResultRows));
        status.set(StatusKind::Done, std::move(text));
    }

    // A bad query leaves the last good result on screen, the way any SQL tool does: only the
    // status line and the mark in the editor change.
    void show_error(const QueryOutcome& outcome) {
        status.set(StatusKind::Error, outcome.error);
        editor.mark(error_mark(outcome.sql, outcome.error));
    }

    void on_runner_event(const RunnerEvent& event) {
        switch (event.kind) {
            case RunnerEvent::Kind::Loading:
                status.set_loading(event.done, event.total);
                break;
            case RunnerEvent::Kind::Ready:
                if (!auto_ran_) {   // the editor opens on a query: show its result unasked
                    auto_ran_ = true;
                    submit(editor.sql());
                    break;
                }
                status.set(StatusKind::Idle,
                           fmt::format("{} trips ready - Ctrl+Enter runs the query",
                                       with_thousands(static_cast<std::int64_t>(status.loaded()))));
                break;
            case RunnerEvent::Kind::Running:
                if (status.status().kind == StatusKind::Running)
                    status.set(StatusKind::Running, running_status(event.elapsed));
                break;
        }
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(settings.layout, {
                {"schema", "Schema", [&](app::WidgetTree::ViewBuilder& p) { p.block(schema); }},
                {"queries", "Saved queries", [&](app::WidgetTree::ViewBuilder& p) { p.block(queries); }},
                {"editor", "Query", [&](app::WidgetTree::ViewBuilder& p) { p.block(editor); }},
                {"results", "Results", [&](app::WidgetTree::ViewBuilder& p) { p.block(results); }},
            });
            vb.status_bar([&] { status.view(vb); });
        });
    }

    // --- the SVG capture -----------------------------------------------------------------

    void seed_demo_data() {
        auto trips = make_trips(kCaptureRows);
        auto outcome = run_synchronously(trips, kSavedQueries[kOpeningQuery].sql);
        status.set_loaded(kCaptureRows);
        results.show(std::move(outcome.result));
        queries.select(kOpeningQuery);
        status.set(StatusKind::Done, done_status(results.rows(), outcome.elapsed_s));
    }
};

int main(int argc, char* argv[]) {
    DataStudioApp app;
    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 1180, 760);
    }

    app::run(app, {.title = "PRISM Data Studio", .width = 1180, .height = 760,
                   .settings = &app.settings, .organization = "prism", .name = "data-studio"},
             [&app](app::AppContext& ctx) {
                       // The busy progress bar asks for a frame every 33 ms through node.redraw_at;
                       // a running clock is what turns those requests into frames.
                       ctx.clock().add([](auto) { return true; });
                       app.start_runner(ctx.waker());
                   });
    return 0;
}
