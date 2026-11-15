#include <prism/prism.hpp>
#include <prism/app/settings.hpp>
#include <prism/backends/settings_dir.hpp>
#include <prism/ui/dock.hpp>
#include "lab_bench_model.hpp"
#include "../showcase/showcase_common.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <thread>
#include <vector>

using namespace prism;
using namespace prism::lab;

namespace {

using steady = std::chrono::steady_clock;

constexpr auto kStall = std::chrono::milliseconds{2000};   // a USB-TMC transaction that hangs
constexpr auto kPollPeriod = std::chrono::milliseconds{25}; // ~40 Hz sampling
constexpr auto kStopSlice = std::chrono::milliseconds{20};

// A blocking hardware read that still notices a stop request: quitting must not wait out a stall.
void stall_for(const std::stop_token& stop, std::chrono::milliseconds total) {
    auto end = steady::now() + total;
    while (steady::now() < end && !stop.stop_requested()) std::this_thread::sleep_for(kStopSlice);
}

std::string stamp(double seconds) { return fmt::format("[{:7.3f}s]", seconds); }

} // namespace

// Glaze reflects this struct: every member is a key, a nested struct a table, an enum written by
// name. Keep it out of an anonymous namespace (Glaze needs a type with linkage).
struct LabBenchPrefs {
    core::Field<ui::DockLayout> layout{{ui::dock_split(/*vertical=*/false, {
        ui::dock_split(/*vertical=*/true, {
            ui::dock_group({"scope"}),
            ui::dock_group({"event_log", "diagnostics"})
        }, {0.72f, 0.28f}),
        ui::dock_split(/*vertical=*/true, {
            ui::dock_group({"power_supply"}),
            ui::dock_group({"wave_gen"})
        }, {0.52f, 0.48f})
    }, {0.60f, 0.40f})}};
    app::WindowGeometry window;
};

// The instrument's screen: drawn from the frame the model carries, with the scale the header
// prints computed from the trace itself (see DaqTelemetry::v_per_div).
namespace lab_bench_detail {

inline const core::Color kScreen = core::Color::rgba(10, 14, 18);
inline const core::Color kGridMinor = core::Color::rgba(38, 56, 64);
inline const core::Color kGridMajor = core::Color::rgba(64, 92, 104);
inline const core::Color kTrace = core::Color::rgba(70, 255, 190);
inline const core::Color kGlow = core::Color::rgba(0, 255, 170, 55);
inline const core::Color kLabel = core::Color::rgba(115, 155, 165);
inline const core::Color kReadout = core::Color::rgba(175, 220, 230);
inline const core::Color kFault = core::Color::rgba(255, 90, 90);

constexpr float kHeaderHeight = 26.f;   // clear of the top ruler label
constexpr float kGutterWidth = 44.f;   // volts/div labels live here
constexpr float kRulerHeight = 16.f;   // time labels live here
constexpr float kPadding = 6.f;

core::Rect plot_area(core::Rect bounds) {
    return {{bounds.origin.x + core::DX{kGutterWidth}, bounds.origin.y + core::DY{kHeaderHeight}},
            {core::Width{bounds.extent.w.raw() - kGutterWidth - kPadding},
             core::Height{bounds.extent.h.raw() - kHeaderHeight - kRulerHeight - kPadding}}};
}

// A 1 px line on a whole coordinate lands between pixels and fades out when the backend scales
// the frame; half-pixel offsets keep the graticule crisp at any window size.
float crisp(float v) { return std::floor(v) + 0.5f; }

std::string scaled(double seconds) {
    double magnitude = std::abs(seconds);
    if (magnitude == 0.0) return "0";
    if (magnitude < 1e-3) return fmt::format("{:.0f} us", seconds * 1e6);
    if (magnitude < 1e-2) return fmt::format("{:.2f} ms", seconds * 1e3);
    return fmt::format("{:.1f} ms", seconds * 1e3);
}

void draw_graticule(render::DrawList& dl, core::Rect bounds, core::Rect plot, const DaqTelemetry& t) {
    float x0 = plot.origin.x.raw();
    float y0 = plot.origin.y.raw();
    float w = plot.extent.w.raw();
    float h = plot.extent.h.raw();
    float dx = w / static_cast<float>(kScopeHDivisions);
    float dy = h / static_cast<float>(kScopeVDivisions);

    for (int i = 1; i < kScopeHDivisions; ++i) {
        float x = crisp(x0 + static_cast<float>(i) * dx);
        core::Color c = (i == kScopeHDivisions / 2) ? kGridMajor : kGridMinor;
        dl.line({core::X{x}, plot.origin.y}, {core::X{x}, plot.origin.y + core::DY{h}}, c, 1.f);
    }
    for (int j = 1; j < kScopeVDivisions; ++j) {
        float y = crisp(y0 + static_cast<float>(j) * dy);
        core::Color c = (j == kScopeVDivisions / 2) ? kGridMajor : kGridMinor;
        dl.line({plot.origin.x, core::Y{y}}, {plot.origin.x + core::DX{w}, core::Y{y}}, c, 1.f);
    }

    // The rulers: what one division is worth, on both axes. Without them a grid is decoration.
    float gutter_center = bounds.origin.x.raw() + kGutterWidth * 0.5f;
    for (int j = 0; j < kScopeVDivisions; j += 2) {
        double volts = static_cast<double>(kScopeVDivisions / 2 - j) * t.v_per_div;
        dl.text(fmt::format("{:+.1f}", volts),
                {core::X{gutter_center}, core::Y{y0 + static_cast<float>(j) * dy}}, 9.5f, kLabel,
                0.f, render::TextAnchor::Center);
    }
    float ruler_y = y0 + h + 2.f;
    for (int i = 1; i < kScopeHDivisions; i += 2) {
        double seconds = static_cast<double>(i - kScopeHDivisions / 2) * t.time_per_div;
        dl.text(scaled(seconds),
                {core::X{x0 + static_cast<float>(i) * dx}, core::Y{ruler_y}}, 9.5f, kLabel,
                0.f, render::TextAnchor::Center);
    }

    // Trigger: the sweep starts on a rising edge half a screen before the centre.
    float trigger_x = crisp(x0 + w * 0.5f);
    dl.line({core::X{trigger_x}, plot.origin.y}, {core::X{trigger_x}, core::Y{y0 + 7.f}}, kGridMajor, 1.f);
    dl.line({core::X{trigger_x}, core::Y{y0 + h - 7.f}}, {core::X{trigger_x}, core::Y{y0 + h}}, kGridMajor, 1.f);
}

void draw_trace(render::DrawList& dl, core::Rect plot, const DaqTelemetry& t, std::vector<core::Point>& scratch) {
    const auto& samples = t.scope_samples;
    if (samples.size() < 2) return;

    float x0 = plot.origin.x.raw();
    float center_y = plot.origin.y.raw() + plot.extent.h.raw() * 0.5f;
    float half_height = plot.extent.h.raw() * 0.5f;
    float step = plot.extent.w.raw() / static_cast<float>(samples.size() - 1);
    float full_scale = static_cast<float>(kScopeVDivisions / 2) * static_cast<float>(t.v_per_div);

    scratch.clear();
    scratch.reserve(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float normalized = std::clamp(samples[i] / full_scale, -1.f, 1.f);
        scratch.push_back({core::X{x0 + static_cast<float>(i) * step},
                           core::Y{center_y - normalized * half_height}});
    }
    dl.polyline(scratch, kGlow, 4.f);
    dl.polyline(std::move(scratch), kTrace, 1.5f);   // polyline takes its points by value
}

void draw_readouts(render::DrawList& dl, core::Rect bounds, core::Rect plot, const DaqTelemetry& t) {
    using render::TextAnchor;
    using render::FontRole;
    float header_y = bounds.origin.y.raw() + 4.f;

    std::string scales = fmt::format("CH1 {:.2f} V/div   {}/div   TRIG auto rising   {}", t.v_per_div,
                                     scaled(t.time_per_div),
                                     ui::enum_label<WaveformType>(ui::enum_index(t.wave_type)));
    dl.text(scales, {bounds.origin.x + core::DX{8.f}, core::Y{header_y}}, 11.f, kReadout, 0.f, TextAnchor::TopLeft, FontRole::Mono);

    std::string measured = fmt::format("Vpp {:5.2f} V  Vrms {:5.2f} V  f {:6.2f} kHz  {}",
                                       t.measured_vpp, t.measured_vrms, t.measured_freq * 1e-3,
                                       t.constant_current ? "CC" : "CV");
    dl.text(measured, {plot.origin.x + core::DX{6.f}, core::Y{plot.origin.y.raw() + plot.extent.h.raw() - 30.f}},
            10.f, kReadout, 0.f, TextAnchor::TopLeft, FontRole::Mono);
}

void draw_fault_banner(render::DrawList& dl, core::Rect bounds, const DaqTelemetry& t) {
    if (!t.fault_tripped) return;
    core::Rect band{bounds.origin, {bounds.extent.w, core::Height{18.f}}};
    dl.filled_rect(band, core::Color::rgba(70, 16, 16));
    dl.text("OVER-CURRENT PROTECTION TRIPPED - OUTPUT DISABLED",
            {bounds.origin.x + core::DX{8.f}, bounds.origin.y + core::DY{4.f}}, 10.f, kFault, 0.f,
            render::TextAnchor::TopLeft, render::FontRole::Mono);
}

} // namespace lab_bench_detail

struct LabBenchApp {
    LabBenchPrefs prefs;

    // The bench, on the logic thread: each control is a Field, and is a widget by itself. A
    // slider prints its own caption and value, to its step's precision.
    core::Field<ui::Slider<double>> target_voltage{
        {.value = 12.0, .min = 0.0, .max = 30.0, .step = 0.1, .label = "Voltage", .unit = "V"}};
    core::Field<ui::Slider<double>> current_limit{
        {.value = 2.0, .min = 0.1, .max = 5.0, .step = 0.05, .label = "Current limit", .unit = "A"}};
    core::Field<ui::Button> output_toggle{{.text = "Output", .primary = true, .checked = true}};
    core::Field<WaveformType> wave_type{WaveformType::Sine};
    core::Field<ui::Slider<double>> frequency{
        {.value = 1000.0, .min = 50.0, .max = 20000.0, .step = 50.0, .label = "Frequency", .unit = "Hz"}};
    core::Field<ui::Slider<double>> amplitude{
        {.value = 5.0, .min = 0.5, .max = 10.0, .step = 0.1, .label = "Amplitude", .unit = "Vpp"}};
    core::Field<ui::Slider<double>> offset{
        {.value = 0.0, .min = -4.0, .max = 4.0, .step = 0.1, .label = "Offset", .unit = "V"}};

    // What the sliders cannot print themselves: values computed from several of them.
    core::Derived<std::string> power_caption{[this] {
        return fmt::format("Max power {:>5.1f} W", target_voltage.get().value * current_limit.get().value);
    }, target_voltage, current_limit};
    core::Derived<std::string> period_caption{[this] {
        return fmt::format("Period  {:.3f} ms   {:.1f} cycles on screen", 1e3 / frequency.get().value,
                           frequency.get().value * kScopeWindowSeconds);
    }, frequency};

    // The link to the hardware thread; none of it is a widget. `setpoints` is one value computed
    // from every control, on the logic thread, and handed over whole. Shared<T> coalesces (frames,
    // the latest setpoints, the fault line); Channel<T> keeps order and drops nothing (events). A
    // Field belongs to the logic thread alone, so neither is one. A stall is a one-shot request,
    // taken and cleared in one step: an atomic exchange.
    struct HardwareLink {
        core::Derived<DaqSettings> setpoints;
        core::Shared<DaqSettings> setpoints_for_worker{};
        std::atomic<bool> stall_request{false};
        core::Shared<bool> fault_request{false};
        core::Shared<DaqTelemetry> telemetry_ingest{};
        core::Channel<DaqEvent> event_ingest{};
    };
    HardwareLink bus{.setpoints = {[this] {
        return DaqSettings{.target_voltage = target_voltage.get().value,
                           .current_limit = current_limit.get().value,
                           .output_enabled = output_toggle.get().checked.value_or(false),
                           .wave_type = wave_type.get(),
                           .frequency_hz = frequency.get().value,
                           .amplitude_vpp = amplitude.get().value,
                           .offset_v = offset.get().value};
    }, target_voltage, current_limit, output_toggle, wave_type, frequency, amplitude, offset}};

    // The frame the screen and the readouts are a function of. The hardware thread cannot set it
    // (a Field belongs to the logic thread), so the drain observer copies it over -- and the canvas
    // depends on it, which is what repaints the screen. No separate repaint token.
    core::Field<DaqTelemetry> frame{};
    steady::time_point last_frame_at_{};
    std::vector<core::Point> trace_scratch_{};

    core::Derived<std::string> status_scalars{[this] {
        const DaqTelemetry& t = frame.get();
        if (t.fault_tripped) return fmt::format("OCP tripped  {:.0f} C", t.temperature_c);
        if (!t.scope_samples.empty() && t.measured_vpp == 0.0) return std::string{"output off"};
        return fmt::format("{:.2f}V  {:.2f}A  {:.1f}W  {:.0f}C", t.voltage_sense, t.current_sense,
                           t.power_watts, t.temperature_c);
    }, frame};
    core::Field<ui::Label<>> status_device{{"simulated USB-TMC | /dev/usbtmc0"}};
    core::Field<ui::Label<>> status_latency{{"frame age 0.0 s"}};
    core::Derived<std::string> mode_caption{[this] {
        const DaqTelemetry& t = frame.get();
        if (t.fault_tripped) return std::string{"Mode  protection tripped"};
        if (!(t.current_sense > 0.0)) return std::string{"Mode  output off"};
        return fmt::format("Mode  {}   load {:.2f} ohm", t.constant_current ? "CC" : "CV",
                           t.voltage_sense / t.current_sense);
    }, frame};

    // Event log.
    core::List<std::string> event_log{};
    core::Field<ui::Label<>> stall_hint{
        {"Both stall buttons block for two seconds. One blocks the hardware thread; the other "
         "blocks the thread that owns the model, which is what a GUI toolkit without a separate "
         "renderer and model thread looks like.", /*heading=*/false, /*wrap=*/true}};

    // Buttons.
    core::Field<ui::Button> lag_btn{{.text = "Stall the hardware thread (2 s)", .primary = false}};
    core::Field<ui::Button> block_btn{{.text = "Stall the logic thread (2 s)", .primary = false}};
    core::Field<ui::Button> trip_fault_btn{{.text = "Trip over-current protection", .primary = false}};
    core::Field<ui::Button> reset_fault_btn{{.text = "Reset protection circuit", .primary = false}};

    steady::time_point start_{steady::now()};
    std::vector<core::Connection> wiring{};

    LabBenchApp() {
        wiring.push_back(output_toggle.on_change().connect([this](const ui::Button& b) {
            push_event(b.checked.value_or(false) ? "DC output enabled" : "DC output disabled", false);
        }));
        wiring.push_back(lag_btn.on_change().connect([this](const auto&) {
            bus.stall_request = true;   // handed to the hardware thread, which does the waiting
            push_event("Stalling the simulated USB-TMC read for 2 s", false);
        }));
        wiring.push_back(block_btn.on_change().connect([this](const auto&) {
            // The comparison this example exists to make: the same blocking call, on the thread
            // that owns the model. Every frame, resize and click waits behind it.
            stall_for(std::stop_token{}, kStall);
            push_event("Logic thread was blocked for 2 s", true);
        }));
        wiring.push_back(trip_fault_btn.on_change().connect([this](const auto&) {
            bus.fault_request.set(true);
            push_event("Over-current protection triggered", true);
        }));
        wiring.push_back(reset_fault_btn.on_change().connect([this](const auto&) {
            bus.fault_request.set(false);
            push_event("Protection circuit reset", false);
        }));

        push_event("PRISM DAQ engine ready", false);
        push_event("Virtual instrument on /dev/usbtmc0", false);
    }

    double now_seconds() const { return std::chrono::duration<double>(steady::now() - start_).count(); }

    void push_event(std::string_view msg, bool is_error) {
        std::string line = fmt::format("{} {} {}", stamp(now_seconds()), is_error ? "[ERR ]" : "[INFO]", msg);
        event_log.push_back(line);
        if (event_log.size() > 150) event_log.erase(0);
    }

    void ingest_telemetry(const DaqTelemetry& t) {
        frame.set(t);
        last_frame_at_ = steady::now();
    }

    void ingest_event(const DaqEvent& e) { push_event(e.message, e.is_error); }

    // Called by the app on the logic thread each time a waker fires: this is where the hardware
    // thread's values cross over and the observers above run.
    void drain() {
        bus.telemetry_ingest.drain_notifications();
        bus.event_ingest.drain_notifications();
    }

    // Per-frame, on the logic thread: the age of what the screen shows. After a hardware stall it
    // climbs to 2 s while the window stays interactive -- the claim, made visible.
    void on_frame() {
        // Tenths of a second: the label changes only when the age does, so an idle bench is idle.
        double age_s = std::chrono::duration<double>(steady::now() - last_frame_at_).count();
        status_latency.set({fmt::format("frame age {:.1f} s", std::floor(age_s * 10.0) / 10.0)});
    }

    void seed_demo_data() { frame.set(synthesize_telemetry(bus.setpoints.get(), 0.42)); }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        using namespace lab_bench_detail;
        const DaqTelemetry& t = frame.get();
        dl.filled_rect(bounds, kScreen);
        core::Rect plot = plot_area(bounds);
        draw_graticule(dl, bounds, plot, t);
        draw_trace(dl, plot, t, trace_scratch_);
        draw_readouts(dl, bounds, plot, t);
        draw_fault_banner(dl, bounds, t);
        dl.rounded_rect(bounds, node.theme->border, 0.f, 1.f);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.dock(prefs.layout, {
                {"scope", "Oscilloscope - CH1", [&](app::WidgetTree::ViewBuilder& p) {
                    p.canvas(*this).depends_on(frame);
                }},
                {"power_supply", "DC Power Supply", [&](app::WidgetTree::ViewBuilder& p) {
                    p.vstack([&] {
                        p.widget(output_toggle).tooltip("Enable or disable the DC output");
                        p.widget(target_voltage);
                        p.widget(current_limit);
                        p.widget(power_caption);
                        p.widget(mode_caption);
                    });
                }},
                {"wave_gen", "Waveform Generator", [&](app::WidgetTree::ViewBuilder& p) {
                    p.vstack([&] {
                        p.widget(wave_type).tooltip("Signal generator waveform shape");
                        p.widget(frequency);
                        p.widget(amplitude);
                        p.widget(offset);
                        p.widget(period_caption);
                    });
                }},
                {"event_log", "Event Log", [&](app::WidgetTree::ViewBuilder& p) { p.list(event_log); }},
                {"diagnostics", "Stress & Fault Injection", [&](app::WidgetTree::ViewBuilder& p) {
                    p.scroll([&] {
                        p.widget(lag_btn).tooltip("The hardware thread waits; the window does not");
                        p.widget(block_btn).tooltip("The logic thread waits: the window stops responding");
                        p.widget(trip_fault_btn);
                        p.widget(reset_fault_btn);
                        p.widget(stall_hint);
                    });
                }}
            });

            vb.status_bar([&] {
                vb.widget(status_device);
                vb.spacer();
                vb.widget(status_scalars);
                vb.gap(16.f);
                vb.widget(status_latency);
            });
        });
    }
};

// The simulated instrument's blocking read. Pure logic is in synthesize_telemetry(); this is the
// glue that makes it take time, which is the only thing it adds.
DaqTelemetry hardware_read(const DaqSettings& settings, double time_sec, const std::stop_token& stop,
                           std::atomic<bool>& stall_request) {
    if (stall_request.exchange(false)) stall_for(stop, kStall);
    return synthesize_telemetry(settings, time_sec);
}

// What the hardware thread reports through the Channel: state changes it observed, not polled
// guesses. Ordered and lossless, unlike the coalescing Shared the frames travel in.
void report_transitions(core::Channel<DaqEvent>& out, double time_sec, const DaqTelemetry& before,
                        const DaqTelemetry& after) {
    auto send = [&](std::string message, bool is_error) {
        out.send({.timestamp = stamp(time_sec), .message = std::move(message), .is_error = is_error});
    };
    if (after.fault_tripped != before.fault_tripped)
        send(after.fault_tripped ? "hardware: over-current protection tripped" : "hardware: protection reset",
             after.fault_tripped);
    if (after.constant_current != before.constant_current)
        send(after.constant_current ? "hardware: supply entered constant-current mode"
                                    : "hardware: supply entered constant-voltage mode",
             false);
}

int main(int argc, char* argv[]) {
    LabBenchApp app;

    if (argc >= 2) {
        app.seed_demo_data();
        return showcase(argc, argv, app, 960, 680);
    }

    std::jthread worker;
    app::SettingsFile settings{app.prefs, backends::settings_dir("prism", "lab-bench") / "settings.toml"};

    app::model_app(app.prefs.window.applied_to({.title = "PRISM Lab Bench & DAQ Console", .width = 960,
                                                .height = 680, .decoration = app::DecorationMode::Custom}),
                   app, [&](app::AppContext& ctx) {
        ctx.remember_window_size(app.prefs.window);

        app.bus.setpoints.observe([&app](const DaqSettings& s) { app.bus.setpoints_for_worker.set(s); });
        app.bus.setpoints_for_worker.set(app.bus.setpoints.get());

        app.bus.telemetry_ingest.set_waker(ctx.waker());
        app.bus.event_ingest.set_waker(ctx.waker());
        app.bus.telemetry_ingest.observe([&app](const DaqTelemetry& t) { app.ingest_telemetry(t); });
        app.bus.event_ingest.observe([&app](const DaqEvent& e) { app.ingest_event(e); });

        app.last_frame_at_ = steady::now();
        ctx.clock().add([&app](auto) {
            app.on_frame();
            return true;
        });

        worker = std::jthread([&app](std::stop_token stop) {
            DaqTelemetry previous{};
            while (!stop.stop_requested()) {
                DaqSettings settings = app.bus.setpoints_for_worker.get();
                settings.simulate_fault = app.bus.fault_request.get();
                DaqTelemetry reading = hardware_read(settings, app.now_seconds(), stop, app.bus.stall_request);
                app.bus.telemetry_ingest.set(reading);
                report_transitions(app.bus.event_ingest, reading.captured_at, previous, reading);
                previous = reading;
                std::this_thread::sleep_for(kPollPeriod);
            }
        });
    });

    return 0;
}
