#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../showcase/showcase_common.hpp"
#include "blocks/diagnostics.hpp"
#include "blocks/event_log.hpp"
#include "blocks/power_supply.hpp"
#include "blocks/scope.hpp"
#include "blocks/status_bar.hpp"
#include "blocks/wave_generator.hpp"
#include "daq/instrument.hpp"
#include "daq/model.hpp"
#include "hardware_thread.hpp"
#include "settings.hpp"
#if PRISM_LAB_BENCH_LINK
#include "scpi_link.hpp"
#endif

using namespace prism;

// The app is composition only: five panels, the one settings value that reaches the hardware
// thread, and the worker loop. What the instrument computes is daq/, how it is drawn is blocks/.
struct LabBenchApp {
    LabBenchSettings settings;

    // The frame the screen and the readouts are a function of. The hardware thread cannot set it
    // (a Field belongs to the logic thread), so the drain observer copies it over -- and the canvas
    // depends on it, which is what repaints the screen. No separate repaint token.
    struct Feed {
        core::Field<lab::DaqTelemetry> frame{};
    } feed{};

    // Declared before the panels that read the frame.
    lab::blocks::Scope scope{feed.frame};
    lab::blocks::EventLog events{};
    lab::blocks::PowerSupply supply{feed.frame, events};
    lab::blocks::WaveGenerator wave_gen{};

    // The link to the hardware thread; none of it is a widget. `setpoints` is one value computed
    // from every control, on the logic thread, and handed over whole. Shared<T> coalesces (frames,
    // the latest setpoints, the fault line); Channel<T> keeps order and drops nothing (events). A
    // Field belongs to the logic thread alone, so neither is one. A stall is a one-shot request,
    // taken and cleared in one step: an atomic exchange.
    struct Link {
        core::Derived<lab::DaqSettings> setpoints;
        core::Shared<lab::DaqSettings> setpoints_for_worker{};
        std::atomic<bool> stall_request{false};
        core::Shared<bool> fault_request{false};
        core::Shared<lab::DaqTelemetry> telemetry_ingest{};
        core::Channel<lab::DaqEvent> event_ingest{};
    };
    Link bus{.setpoints = {[this] {
        lab::DaqSettings settings;
        supply.apply_to(settings);
        wave_gen.apply_to(settings);
        return settings;
    }, supply.target_voltage, supply.current_limit, supply.output_toggle, wave_gen.wave_type,
       wave_gen.frequency, wave_gen.amplitude, wave_gen.offset}};

    lab::blocks::Diagnostics diagnostics{bus.stall_request, bus.fault_request, events};
    lab::blocks::StatusBar status{feed.frame};

    LabBenchApp() {
        events.push("PRISM DAQ engine ready");
        events.push("Virtual instrument on /dev/usbtmc0");
    }

    void ingest_telemetry(const lab::DaqTelemetry& t) {
        feed.frame.set(t);
        status.frame_arrived();
    }

    void ingest_event(const lab::DaqEvent& e) { events.push(e.message, e.is_error); }

    void seed_demo_data() { ingest_telemetry(lab::synthesize_telemetry(bus.setpoints.get(), 0.42)); }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(settings.layout, {
                {"scope", "Oscilloscope - CH1", [&](app::WidgetTree::ViewBuilder& p) { p.block(scope); }},
                {"power_supply", "DC Power Supply",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(supply); }},
                {"wave_gen", "Waveform Generator",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(wave_gen); }},
                {"event_log", "Event Log", [&](app::WidgetTree::ViewBuilder& p) { p.block(events); }},
                {"diagnostics", "Stress & Fault Injection",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(diagnostics); }},
            });
            vb.status_bar([&] { vb.block(status); });
        });
    }
};

namespace {

// One reading of the instrument for the hardware thread: the local simulator, or one over a link.
// nullopt: nothing this time (a link down), and the thread just tries again next period.
using ReadInstrument =
    std::function<std::optional<lab::DaqTelemetry>(const lab::DaqSettings&, double time_sec, const std::stop_token&)>;

void run_hardware_thread(LabBenchApp& app, const std::stop_token& stop, ReadInstrument read) {
    const auto started = lab::steady::now();
    lab::DaqTelemetry previous{};
    while (!stop.stop_requested()) {
        lab::DaqSettings requested = app.bus.setpoints_for_worker.get();
        requested.simulate_fault = app.bus.fault_request.get();
        const double time_sec = std::chrono::duration<double>(lab::steady::now() - started).count();
        if (auto reading = read(requested, time_sec, stop)) {
            app.bus.telemetry_ingest.set(*reading);
            lab::report_transitions(app.bus.event_ingest, previous, *reading);
            previous = *reading;
        }
        std::this_thread::sleep_for(lab::kPollPeriod);
    }
}

ReadInstrument local_instrument(LabBenchApp& app) {
    return [&app](const lab::DaqSettings& s, double time_sec, const std::stop_token& stop) {
        return std::optional{lab::hardware_read(s, time_sec, stop, app.bus.stall_request)};
    };
}

#if PRISM_LAB_BENCH_LINK
// Connects on first use and again after the link drops; the event log says when either happens.
ReadInstrument remote_instrument(LabBenchApp& app, std::string host, std::uint16_t port) {
    struct Link {
        std::optional<lab::RemoteInstrument> instrument;
        bool said_unreachable = false;
    };
    auto link = std::make_shared<Link>();
    const std::string where = host + ":" + std::to_string(port);
    return [&app, link, host, port, where](const lab::DaqSettings& s, double, const std::stop_token& stop)
               -> std::optional<lab::DaqTelemetry> {
        if (!link->instrument) {
            auto connected = lab::RemoteInstrument::connect(host, port);
            if (!connected) {
                if (!std::exchange(link->said_unreachable, true))
                    app.bus.event_ingest.send({.message = "link: no instrument at " + where, .is_error = true});
                lab::stall_for(stop, std::chrono::milliseconds{1000});
                return std::nullopt;
            }
            link->instrument.emplace(std::move(*connected));
            link->said_unreachable = false;
            app.bus.event_ingest.send({.message = "link: connected to " + where});
        }
        auto reading = link->instrument->read(s, app.bus.stall_request.exchange(false));
        if (!reading) {
            app.bus.event_ingest.send({.message = "link: lost (" + reading.error().message() + ")", .is_error = true});
            link->instrument.reset();
            return std::nullopt;
        }
        return std::move(*reading);
    };
}

// `--serve-instrument [host:]port`: the simulated instrument alone, no window, until killed.
int serve_instrument(std::string_view where) {
    auto endpoint = where.contains(':') ? lab::parse_endpoint(where) : lab::parse_endpoint("127.0.0.1:" + std::string(where));
    if (!endpoint) return std::fprintf(stderr, "expected [host:]port, got %.*s\n", int(where.size()), where.data()), 2;
    auto listener = io::TcpListener::bind(endpoint->first, endpoint->second);
    if (!listener) return std::fprintf(stderr, "cannot listen: %s\n", listener.error().message().c_str()), 1;
    std::printf("simulated instrument on %s:%u (SCPI, one line per command)\n", endpoint->first.c_str(), listener->port());
    std::fflush(stdout);
    const auto started = lab::steady::now();
    std::stop_source forever;
    lab::serve_instrument(*listener, forever.get_token(),
                          [started] { return std::chrono::duration<double>(lab::steady::now() - started).count(); });
    return 0;
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
#if PRISM_LAB_BENCH_LINK
    if (args.size() == 2 && args[0] == "--serve-instrument") return serve_instrument(args[1]);
    std::optional<std::pair<std::string, std::uint16_t>> remote;
    if (args.size() == 2 && args[0] == "--instrument") {
        remote = lab::parse_endpoint(args[1]);
        if (!remote) return std::fprintf(stderr, "--instrument expects host:port\n"), 2;
    }
#else
    const bool remote = false;
#endif

    LabBenchApp app;

    if (!remote && argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 960, 680);
    }

    std::jthread worker;

    app::run(app, {.title = "PRISM Lab Bench & DAQ Console", .width = 960, .height = 680,
                   .settings = &app.settings, .organization = "prism", .name = "lab-bench"},
             [&](app::AppContext& ctx) {
        app.bus.setpoints.observe([&app](const lab::DaqSettings& s) { app.bus.setpoints_for_worker.set(s); });
        app.bus.setpoints_for_worker.set(app.bus.setpoints.get());

        ctx.receive(app.bus.telemetry_ingest, [&app](const lab::DaqTelemetry& t) { app.ingest_telemetry(t); });
        ctx.receive(app.bus.event_ingest, [&app](const lab::DaqEvent& e) { app.ingest_event(e); });

        ctx.clock().add([&app](auto) {
            app.status.tick();
            return true;
        });

        ReadInstrument read = local_instrument(app);
#if PRISM_LAB_BENCH_LINK
        if (remote) read = remote_instrument(app, remote->first, remote->second);
#endif
        worker = std::jthread([&app, read](std::stop_token stop) { run_hardware_thread(app, stop, read); });
    });

    return 0;
}
