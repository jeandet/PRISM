#include <prism/prism.hpp>
#include <prism/render/svg_export.hpp>
#include "../showcase/showcase_common.hpp"

#include <fmt/format.h>
#include <array>
#include <cmath>
#include <fstream>
#include <string>

using namespace prism;

// --- Domain types ---

enum class WaveShape { Sine, Square, Triangle, Sawtooth };

static float wave_value(WaveShape shape, float phase) {
    constexpr float pi = 3.14159265f;
    switch (shape) {
        case WaveShape::Sine:
            return std::sin(2.f * pi * phase);
        case WaveShape::Square:
            return phase < 0.5f ? 1.f : -1.f;
        case WaveShape::Triangle:
            return 4.f * std::abs(phase - 0.5f) - 1.f;
        case WaveShape::Sawtooth:
            return 2.f * phase - 1.f;
    }
    return 0.f;
}

static float rms_value(WaveShape shape) {
    switch (shape) {
        case WaveShape::Sine:     return 1.f / std::sqrt(2.f);
        case WaveShape::Square:   return 1.f;
        case WaveShape::Triangle: return 1.f / std::sqrt(3.f);
        case WaveShape::Sawtooth: return 1.f / std::sqrt(3.f);
    }
    return 0.f;
}

// --- Waveform drawing (shared by canvas + SVG export) ---

struct WaveParams {
    WaveShape shape;
    float freq;
    float amp;
    bool harmonics;
};

static void draw_waveform(render::DrawList& dl, core::Rect bounds, WaveParams p,
                           core::Color bg, core::Color track,
                           core::Color curve, core::Color marker) {
    auto w = bounds.extent.w.raw();
    auto h = bounds.extent.h.raw();
    float cy = h * 0.5f;

    dl.rounded_rect(bounds, bg, 6.f);

    dl.line(core::Point{core::X{0}, core::Y{cy}},
            core::Point{core::X{w}, core::Y{cy}},
            track, 1.f);

    int steps = std::max(2, static_cast<int>(w));
    auto sample = [&](float x_pos) -> float {
        float norm = x_pos / w;
        float phase = std::fmod(p.freq * norm, 1.f);
        float y_val = p.amp * wave_value(p.shape, phase);
        if (p.harmonics) {
            y_val += p.amp * 0.33f * wave_value(p.shape, std::fmod(3.f * p.freq * norm, 1.f));
            y_val += p.amp * 0.2f  * wave_value(p.shape, std::fmod(5.f * p.freq * norm, 1.f));
            y_val = std::clamp(y_val, -1.f, 1.f);
        }
        return cy - y_val * h * 0.45f;
    };

    std::vector<core::Point> pts(static_cast<size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        float x = static_cast<float>(i);
        pts[static_cast<size_t>(i)] = {core::X{x}, core::Y{sample(x)}};
    }
    dl.polyline(pts, curve, 2.f);

    constexpr int marker_every = 50;
    for (int i = 0; i < steps; i += marker_every) {
        float x = static_cast<float>(i);
        float y = sample(x);
        if (std::abs(y - cy) > h * 0.3f)
            dl.circle(core::Point{core::X{x}, core::Y{y}}, 3.f, marker);
    }
}

// --- Waveform editor (Tab 1) ---

struct Waveform {
    core::Field<WaveShape> shape{WaveShape::Sine};
    core::Field<ui::Slider<>> frequency{{.value = 2.0, .min = 0.1, .max = 10.0}};
    core::Field<ui::Slider<double, ui::Orientation::Vertical>> amplitude{
        {.value = 0.8, .min = 0.0, .max = 1.0}};
    core::Field<ui::Checkbox> harmonics{{.checked = false, .label = "Show harmonics"}};
    core::Field<ui::Label<>> stats{{""}};
    core::Field<ui::Button> export_svg{{"Export SVG"}};

    WaveParams current_params() const {
        return {shape.get(),
                static_cast<float>(frequency.get().value),
                static_cast<float>(amplitude.get().value),
                harmonics.get().checked};
    }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        auto& t = *node.theme;
        draw_waveform(dl, bounds, current_params(), t.surface, t.track, t.accent, t.accent_hover);
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            vb.widget(amplitude);
            vb.canvas(*this)
                .depends_on(frequency).depends_on(amplitude)
                .depends_on(shape).depends_on(harmonics);
        });
        vb.widget(frequency);
        vb.hstack([&] {
            vb.widget(shape);
            vb.widget(harmonics);
            vb.widget(export_svg);
        });
        vb.widget(stats);
    }
};

// --- Signal data table (Tab 2) ---

struct SignalTable {
    static constexpr size_t N = 50;
    const Waveform& wf;

    size_t column_count() const { return 5; }
    size_t row_count() const { return N; }
    std::string_view header(size_t c) const {
        static constexpr std::array<const char*, 5> h = {
            "Sample", "Time", "Value", "Abs", "Phase"};
        return h[c];
    }
    std::string cell_text(size_t r, size_t c) const {
        auto sh = wf.shape.get();
        float freq = static_cast<float>(wf.frequency.get().value);
        float amp = static_cast<float>(wf.amplitude.get().value);
        float t = static_cast<float>(r) / static_cast<float>(N);
        float phase = std::fmod(freq * t, 1.f);
        float val = amp * wave_value(sh, phase);
        switch (c) {
            case 0: return fmt::to_string(r);
            case 1: return fmt::format("{:.3f}", t);
            case 2: return fmt::format("{:+.4f}", val);
            case 3: return fmt::format("{:.4f}", std::abs(val));
            case 4: return fmt::format("{:.1f}", std::fmod(360.0f * freq * t, 360.0f));
            default: return "";
        }
    }
};

// --- Export progress bar ---

struct ProgressBar {
    core::Field<float> progress{0.f};

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        auto& t = *node.theme;
        dl.rounded_rect(bounds, t.surface, 4.f);
        dl.rounded_rect(bounds, t.border, 4.f, 1.f);
        float fill_w = bounds.extent.w.raw() * std::clamp(progress.get(), 0.f, 1.f);
        if (fill_w > 0.f) {
            dl.rounded_rect(
                core::Rect{bounds.origin,
                            core::Size{core::Width{fill_w}, bounds.extent.h}},
                t.accent_hover, 4.f);
        }
    }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(progress);
    }
};

// --- Root model ---

struct SignalGenerator {
    Waveform waveform;
    SignalTable signal_table{.wf = waveform};
    ProgressBar export_progress;

    core::Field<ui::TextField<>> filename{{.value = "signal_001", .placeholder = "Filename..."}};
    core::Field<ui::Button> generate{{"Export"}};
    core::Field<ui::TextArea<>> notes{{.placeholder = "Annotations...", .rows = 4}};
    core::List<std::string> export_log;
    core::Field<ui::TabBar<>> tabs;

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Waveform", [&](app::WidgetTree::ViewBuilder& tvb) {
                tvb.component(waveform);
            });
            vb.tab("Data", [&](app::WidgetTree::ViewBuilder& tvb) {
                tvb.scroll([&] {
                    tvb.table(signal_table)
                        .depends_on(waveform.frequency)
                        .depends_on(waveform.amplitude)
                        .depends_on(waveform.shape);
                });
            });
            vb.tab("Export", [&](app::WidgetTree::ViewBuilder& tvb) {
                tvb.vstack(filename, generate);
                tvb.component(export_progress);
                tvb.widget(notes);
                tvb.list(export_log);
            });
        });
    }
};

// --- Wiring ---

static std::string shape_name(WaveShape s) {
    switch (s) {
        case WaveShape::Sine:     return "sine";
        case WaveShape::Square:   return "square";
        case WaveShape::Triangle: return "triangle";
        case WaveShape::Sawtooth: return "sawtooth";
    }
    return "?";
}

int main(int argc, char* argv[]) {
    SignalGenerator app;
    std::vector<core::Connection> connections;
    ui::Animation<float> export_anim;
    int export_count = 0;

    // then() callbacks below capture update_stats BY VALUE, not by [&] -- they outlive
    // the setup lambda that registers them (their Connections live in `connections`,
    // a main()-scope local), so a reference into setup's frame would dangle.
    auto update_stats = [&app] {
        auto sh = app.waveform.shape.get();
        float amp = static_cast<float>(app.waveform.amplitude.get().value);
        float freq = static_cast<float>(app.waveform.frequency.get().value);
        float rms = amp * rms_value(sh);
        app.waveform.stats.set({fmt::format("Pk-Pk: {:.2f}  RMS: {:.3f}  f={:.1f} Hz",
                      2.f * amp, rms, freq)});
    };
    update_stats();

    auto setup = [&](app::AppContext& ctx) {
        auto sched = ctx.scheduler();

        connections.push_back(
            app.waveform.frequency.on_change()
            | core::on(sched)
            | core::then([update_stats](const ui::Slider<>&) { update_stats(); })
        );
        connections.push_back(
            app.waveform.amplitude.on_change()
            | core::on(sched)
            | core::then([update_stats](const ui::Slider<double, ui::Orientation::Vertical>&) {
                  update_stats();
              })
        );
        connections.push_back(
            app.waveform.shape.on_change()
            | core::on(sched)
            | core::then([update_stats](const WaveShape&) { update_stats(); })
        );

        // SVG export button: render waveform to SVG file
        connections.push_back(
            app.waveform.export_svg.on_change()
            | core::on(sched)
            | core::then([&app](const ui::Button&) {
                  render::DrawList dl;
                  core::Rect bounds{core::Point{core::X{0}, core::Y{0}},
                                     core::Size{core::Width{800}, core::Height{300}}};
                  draw_waveform(dl, bounds, app.waveform.current_params(),
                                core::Color::rgba(30, 30, 46),
                                core::Color::rgba(88, 91, 112),
                                core::Color::rgba(137, 180, 250),
                                core::Color::rgba(166, 209, 255));
                  auto svg = render::to_svg(dl);
                  auto fname = app.filename.get().value + ".svg";
                  std::ofstream(fname) << svg;
                  app.export_log.push_back("SVG exported -> " + fname);
              })
        );

        // Export button: log entry + animated progress bar.
        // Capture window/clock by pointer — they live inside model_app's stack frame.
        auto& win = ctx.window();
        auto& clock = ctx.clock();
        connections.push_back(
            app.generate.on_change()
            | core::on(sched)
            | core::then([&app, &export_count, &export_anim, &win, &clock](const ui::Button&) {
                  ++export_count;
                  auto sh = app.waveform.shape.get();
                  float freq = static_cast<float>(app.waveform.frequency.get().value);
                  float amp = static_cast<float>(app.waveform.amplitude.get().value);
                  auto fname = app.filename.get().value;

                  app.export_log.push_back(fmt::format(
                      "#{} {} {}Hz amp={} -> {}.wav",
                      export_count, shape_name(sh), freq, amp, fname));

                  win.set_title("Signal Generator — exported " + fname);

                  app.export_progress.progress.set(0.f);
                  export_anim = ui::animate(clock,
                      app.export_progress.progress, 1.f,
                      ui::SpringConfig{.spring = {.stiffness = 80.f, .damping = 10.f}});
              })
        );
    };

    if (argc >= 2) {
        return showcase(argc, argv, app, 1024, 768, std::function<void(app::AppContext&)>(setup));
    }

    app::model_app({.title = "PRISM Signal Generator", .width = 1024, .height = 768,
                       .decoration = app::DecorationMode::Custom},
                     app, setup);
}
