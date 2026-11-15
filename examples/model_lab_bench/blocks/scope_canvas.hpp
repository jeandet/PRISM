#pragma once

#include <prism/prism.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fmt/format.h>
#include <string>
#include <vector>

#include "../daq/model.hpp"

// Everything the scope draws: the graticule and its rulers, the trace, the readouts, the protection
// banner. Free functions over a DrawList, so the block around them (scope.hpp) is just placement.
namespace prism::lab::blocks::scope_canvas {

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

inline core::Rect plot_area(core::Rect bounds) {
    return {{bounds.origin.x + core::DX{kGutterWidth}, bounds.origin.y + core::DY{kHeaderHeight}},
            {core::Width{bounds.extent.w.raw() - kGutterWidth - kPadding},
             core::Height{bounds.extent.h.raw() - kHeaderHeight - kRulerHeight - kPadding}}};
}

// A 1 px line on a whole coordinate lands between pixels and fades out when the backend scales
// the frame; half-pixel offsets keep the graticule crisp at any window size.
inline float crisp(float v) { return std::floor(v) + 0.5f; }

inline std::string scaled(double seconds) {
    double magnitude = std::abs(seconds);
    if (magnitude == 0.0) return "0";
    if (magnitude < 1e-3) return fmt::format("{:.0f} us", seconds * 1e6);
    if (magnitude < 1e-2) return fmt::format("{:.2f} ms", seconds * 1e3);
    return fmt::format("{:.1f} ms", seconds * 1e3);
}

inline void draw_graticule(render::DrawList& dl, core::Rect bounds, core::Rect plot, const DaqTelemetry& t) {
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

inline void draw_trace(render::DrawList& dl, core::Rect plot, const DaqTelemetry& t,
                       std::vector<core::Point>& scratch) {
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

inline void draw_readouts(render::DrawList& dl, core::Rect bounds, core::Rect plot, const DaqTelemetry& t) {
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

inline void draw_fault_banner(render::DrawList& dl, core::Rect bounds, const DaqTelemetry& t) {
    if (!t.fault_tripped) return;
    core::Rect band{bounds.origin, {bounds.extent.w, core::Height{18.f}}};
    dl.filled_rect(band, core::Color::rgba(70, 16, 16));
    dl.text("OVER-CURRENT PROTECTION TRIPPED - OUTPUT DISABLED",
            {bounds.origin.x + core::DX{8.f}, bounds.origin.y + core::DY{4.f}}, 10.f, kFault, 0.f,
            render::TextAnchor::TopLeft, render::FontRole::Mono);
}

inline void draw(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node, const DaqTelemetry& t,
                 std::vector<core::Point>& scratch) {
    dl.filled_rect(bounds, kScreen);
    core::Rect plot = plot_area(bounds);
    draw_graticule(dl, bounds, plot, t);
    draw_trace(dl, plot, t, scratch);
    draw_readouts(dl, bounds, plot, t);
    draw_fault_banner(dl, bounds, t);
    dl.rounded_rect(bounds, node.theme->border, 0.f, 1.f);
}

} // namespace prism::lab::blocks::scope_canvas
