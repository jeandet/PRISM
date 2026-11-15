#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>
#include <prism/widgets/plot.hpp>
#include "proc_metrics.hpp"
#include "process_table.hpp"
#include "process_tree_source.hpp"
#include "sim_metrics.hpp"
#include "../showcase/showcase_common.hpp"

#include <cmath>
#include <numbers>
#include <thread>

using namespace prism;


// nice_ticks may not always land exactly on 0.0 (floating-point round-off), so "now"
// is matched within half a sample interval rather than with an exact equality check.
// Over a short visible span (e.g. right after the app starts, or the 4-point demo
// dataset) nice_ticks can pick a sub-second step -- rounding every tick to a whole
// second would then print the same label (e.g. "-0:03") for two different ticks, so
// a fractional second is kept whenever the tick isn't itself near a whole second.
std::string format_seconds_ago(double seconds_ago) {
    if (std::abs(seconds_ago) < History::sample_interval_seconds / 2.0) return "now";

    double whole = std::abs(seconds_ago);
    long minutes = static_cast<long>(whole) / 60;
    double secs = whole - static_cast<double>(minutes) * 60.0;
    const char* sign = seconds_ago < 0.0 ? "-" : "+";

    if (std::abs(secs - std::round(secs)) < 0.05)
        return fmt::format("{}{}:{:02d}", sign, minutes, static_cast<long>(std::lround(secs)));
    return fmt::format("{}{}:{:04.1f}", sign, minutes, secs);
}

// What the monitor remembers between runs (app/settings.hpp): its panels' layout and its size.
struct MonitorPrefs {
    // Plots above; the process table, tree and event log as tabs below.
    core::Field<ui::DockLayout> layout{{ui::dock_split(true, {ui::dock_group({"plots"}),
                                                          ui::dock_group({"table", "tree", "events"})})}};
    app::WindowGeometry window;
};

struct SystemMonitor {
    MonitorPrefs prefs;
    // Background-thread ingest points: the sampler threads set them, ctx.receive() in main()
    // hands each value to the logic thread.
    core::Shared<SystemSample> sys_sample{};
    core::Shared<std::vector<ProcessInfo>> proc_list{};
    // Ordered/lossless, unlike the two Shared<T> above: every process start/stop between two
    // drains must show up in the log, not just the latest one -- the reason this is a
    // Channel<T> rather than another Shared<T>.
    core::Channel<ProcessEvent> process_events{};

    History cpu_history;
    History mem_history;
    History net_history;

    plot::PlotGroup plot_group;
    plot::PlotPanel* cpu_plot = &plot_group.add_plot("CPU %");
    plot::PlotPanel* mem_plot = &plot_group.add_plot("Mem (MB)");
    plot::PlotPanel* net_plot = &plot_group.add_plot("Net (KB/s)");

    ProcessTable process_table;
    // Bumped per update so the table redraws (it is a dependency, not a widget: the same
    // "not placed by view()" debug warning as the ingest points above).
    core::Field<std::uint64_t> process_revision{0};
    FlatProcessTreeSource tree_source;
    ui::TreeController tree_ctrl{ui::wrap_tree_storage(tree_source)};

    static constexpr size_t max_event_log_lines = 200;
    core::List<std::string> event_log;

    core::Field<float> heartbeat_phase{0.f};
    core::Field<ui::Label<>> summary{{"Waiting for the first sample…"}};   // the status bar's text
    float last_cpu = 0.f;
    double last_mem_mb = 0.0, total_mem_mb = 0.0;
    std::size_t process_count = 0;

    SystemMonitor() { plot_group.x_tick_format = format_seconds_ago; }

    static void rebuild_plot(plot::PlotPanel& plot, const History& h, bool fill = false) {
        std::vector<double> xs(h.values.size());
        std::vector<double> ys(h.values.begin(), h.values.end());
        double newest_index = static_cast<double>(xs.size()) - 1.0;
        for (size_t i = 0; i < xs.size(); ++i)
            xs[i] = (static_cast<double>(i) - newest_index) * History::sample_interval_seconds;
        auto colors = plot::default_series_colors(ui::default_theme());
        plot.clear_series();
        plot.add_series(plot::XYData{std::move(xs), std::move(ys)},
                        plot::SeriesStyle{colors[0], 2.f, fill, 0.0});
        plot.notify();
    }

    void ingest_system(const SystemSample& s) {
        cpu_history.push(s.cpu_percent);
        mem_history.push(static_cast<float>(s.mem_used_mb));
        net_history.push(static_cast<float>(s.net_rx_kbps));
        rebuild_plot(*cpu_plot, cpu_history, /*fill=*/true);
        rebuild_plot(*mem_plot, mem_history);
        rebuild_plot(*net_plot, net_history);
        last_cpu = s.cpu_percent;
        last_mem_mb = s.mem_used_mb;
        total_mem_mb = s.mem_total_mb;
        refresh_summary();
    }

    void refresh_summary() {
        summary.set({fmt::format("CPU {:.0f}%  ·  memory {:.1f} / {:.1f} GB  ·  {} processes", last_cpu,
                                 last_mem_mb / 1024.0, total_mem_mb / 1024.0, process_count)});
    }

    void ingest_processes(const std::vector<ProcessInfo>& processes) {
        process_table.update(processes);
        process_count = processes.size();
        refresh_summary();
        process_revision.set(process_revision.get() + 1);
        tree_source.update(processes);
        tree_ctrl.refresh();
    }

    void ingest_process_event(const ProcessEvent& e) {
        event_log.push_back(fmt::format("{} pid {} {}", e.started ? "[+]" : "[-]", e.pid, e.name));
        if (event_log.size() > max_event_log_lines) event_log.erase(0);
    }

    void seed_demo_data() {
        struct { float cpu; double mem; double net; } samples[] = {
            {12.5f, 4096.0, 128.0}, {18.0f, 4200.0, 96.0},
            {9.0f, 4150.0, 210.0}, {22.0f, 4300.0, 150.0},
        };
        for (auto& s : samples) {
            ingest_system(SystemSample{.cpu_percent = s.cpu, .mem_used_mb = s.mem,
                                        .mem_total_mb = 16384.0, .net_rx_kbps = s.net,
                                        .net_tx_kbps = 32.0});
        }
        std::vector<ProcessInfo> demo;
        demo.push_back(ProcessInfo{.pid = 1, .ppid = 0, .name = "init",
                                    .cpu_percent = 0.1f, .mem_percent = 0.5f});
        demo.push_back(ProcessInfo{.pid = 42, .ppid = 1, .name = "prism_demo",
                                    .cpu_percent = 3.2f, .mem_percent = 1.8f});
        ingest_processes(demo);
        ingest_process_event(ProcessEvent{.pid = 1, .name = "init", .started = true});
        ingest_process_event(ProcessEvent{.pid = 42, .name = "prism_demo", .started = true});
        ingest_process_event(ProcessEvent{.pid = 17, .name = "old_task", .started = false});
    }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        auto& t = *node.theme;
        float size = 8.f + 4.f * std::sin(heartbeat_phase.get());
        dl.rounded_rect(
            core::Rect{bounds.origin, core::Size{core::Width{size}, core::Height{size}}},
            t.accent_hover, size * 0.5f);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        // The panels are a dock (doc/design/drag-and-docking.md): their arrangement is
        // prefs.layout, saved with the settings; the heartbeat stays below it.
        vb.vstack([&] {
            vb.dock(prefs.layout, {
                {"plots", "Plots", [&](app::WidgetTree::ViewBuilder& p) { plot_group.view(p); }},
                {"table", "Table", [&](app::WidgetTree::ViewBuilder& p) {
                     p.table(process_table).sort(2, /*descending=*/true).depends_on(process_revision);   // CPU %
                 }},
                {"tree", "Tree", [&](app::WidgetTree::ViewBuilder& p) { p.tree(tree_ctrl); }},
                {"events", "Events", [&](app::WidgetTree::ViewBuilder& p) { p.list(event_log); }},
            });
            vb.status_bar([&] {
                vb.canvas(*this).depends_on(heartbeat_phase).min_size(core::Width{24});
                vb.widget(summary);
                vb.spacer();
            });
        });
    }
};

int main(int argc, char* argv[]) {
    SystemMonitor app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 900, 700);
    }

    std::jthread sys_thread;
    std::jthread proc_thread;

    app::run(app, {.title = "PRISM System Monitor", .width = 900, .height = 700,
                   .settings = &app.prefs, .organization = "prism", .name = "system-monitor"},
             [&](app::AppContext& ctx) {
        ctx.clock().add([&app](ui::AnimationClock::time_point now) {
            double t = std::chrono::duration<double>(now.time_since_epoch()).count();
            // now.time_since_epoch() is steady_clock's time since boot, which on a machine
            // with real uptime is huge -- wrap into [0, 2pi) with fmod (double precision)
            // BEFORE casting to float, so the float's ULP stays negligible next to one
            // frame's phase advance. Casting the huge value straight to float would give
            // it a ULP larger than a frame's worth of advance, silently freezing
            // heartbeat_phase.set() (Field<T>::set() no-ops on an unchanged value) and
            // making the heartbeat visibly stutter.
            double phase = std::fmod(t * 4.0, 2.0 * std::numbers::pi); // ~4 rad/s
            app.heartbeat_phase.set(static_cast<float>(phase));
            return true; // the heartbeat runs forever by design
        });
        // What the sampler threads deliver reaches the logic thread as soon as it is set.
        ctx.receive(app.sys_sample, [&app](const SystemSample& s) { app.ingest_system(s); });
        ctx.receive(app.proc_list,
                    [&app](const std::vector<ProcessInfo>& p) { app.ingest_processes(p); });
        ctx.receive(app.process_events,
                    [&app](const ProcessEvent& e) { app.ingest_process_event(e); });

        // The real /proc on Linux; elsewhere (the web, macOS, Windows) a simulated machine.
        if (has_proc()) {
            sys_thread = std::jthread(poll_system_loop, std::ref(app.sys_sample), proc_system_reader());
            proc_thread = std::jthread(poll_processes_loop, std::ref(app.proc_list), std::ref(app.process_events),
                                       read_process_list);
        } else {
            auto t0 = std::chrono::steady_clock::now();
            auto since = [t0] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
            sys_thread = std::jthread(poll_system_loop, std::ref(app.sys_sample),
                                      [since](double) { return simulated_system_sample(since()); });
            proc_thread = std::jthread(poll_processes_loop, std::ref(app.proc_list), std::ref(app.process_events),
                                       [since](const std::vector<ProcessInfo>&, double) { return simulated_processes(since()); });
        }
    });

    return 0;
}
