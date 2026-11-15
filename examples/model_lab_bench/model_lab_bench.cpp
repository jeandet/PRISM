#include <prism/prism.hpp>
#include <prism/app/run.hpp>
#include <prism/ui/dock.hpp>

#include <atomic>
#include <chrono>
#include <thread>

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
#include "prefs.hpp"

using namespace prism;

// The app is composition only: five panels, the one settings value that reaches the hardware
// thread, and the worker loop. What the instrument computes is daq/, how it is drawn is blocks/.
struct LabBenchApp {
    LabBenchPrefs prefs;

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
            vb.dock(prefs.layout, {
                {"scope", "Oscilloscope - CH1", [&](app::WidgetTree::ViewBuilder& p) { p.block(scope); }},
                {"power_supply", "DC Power Supply",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(supply); }},
                {"wave_gen", "Waveform Generator",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(wave_gen); }},
                {"event_log", "Event Log", [&](app::WidgetTree::ViewBuilder& p) { p.block(events); }},
                {"diagnostics", "Stress & Fault Injection",
                 [&](app::WidgetTree::ViewBuilder& p) { p.block(diagnostics); }},
            });
            vb.status_bar([&] { status.view(vb); });
        });
    }
};

int main(int argc, char* argv[]) {
    LabBenchApp app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 960, 680);
    }

    std::jthread worker;

    app::run(app, {.title = "PRISM Lab Bench & DAQ Console", .width = 960, .height = 680,
                   .settings = &app.prefs, .organization = "prism", .name = "lab-bench"},
             [&](app::AppContext& ctx) {
        app.bus.setpoints.observe([&app](const lab::DaqSettings& s) { app.bus.setpoints_for_worker.set(s); });
        app.bus.setpoints_for_worker.set(app.bus.setpoints.get());

        ctx.receive(app.bus.telemetry_ingest, [&app](const lab::DaqTelemetry& t) { app.ingest_telemetry(t); });
        ctx.receive(app.bus.event_ingest, [&app](const lab::DaqEvent& e) { app.ingest_event(e); });

        ctx.clock().add([&app](auto) {
            app.status.tick();
            return true;
        });

        worker = std::jthread([&app](std::stop_token stop) {
            const auto started = lab::steady::now();
            lab::DaqTelemetry previous{};
            while (!stop.stop_requested()) {
                lab::DaqSettings requested = app.bus.setpoints_for_worker.get();
                requested.simulate_fault = app.bus.fault_request.get();
                const double time_sec =
                    std::chrono::duration<double>(lab::steady::now() - started).count();
                lab::DaqTelemetry reading = lab::hardware_read(requested, time_sec, stop, app.bus.stall_request);
                app.bus.telemetry_ingest.set(reading);
                lab::report_transitions(app.bus.event_ingest, previous, reading);
                previous = reading;
                std::this_thread::sleep_for(lab::kPollPeriod);
            }
        });
    });

    return 0;
}
