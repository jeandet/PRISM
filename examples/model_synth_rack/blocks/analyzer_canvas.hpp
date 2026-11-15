#pragma once

#include <prism/prism.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <string>
#include <vector>

#include "../dsp/spectrum.hpp"
#include "../dsp/arpeggiator.hpp"
#include "../dsp/settings.hpp"

// The analyzer, drawn: log-frequency bars on a dB grid, a triggered scope strip, the step
// playhead and the peak meter. Pure drawing -- takes a frame and the settings it was made
// from, touches no model and no threads, so it is easy to reason about on its own.
namespace prism::synth::analyzer_canvas {

inline const core::Color kScreen = core::Color::rgba(8, 11, 16);
inline const core::Color kStrip = core::Color::rgba(14, 19, 26);
inline const core::Color kPanel = core::Color::rgba(24, 32, 42);
inline const core::Color kGrid = core::Color::rgba(28, 39, 52);
inline const core::Color kGridStrong = core::Color::rgba(48, 66, 88);
inline const core::Color kDim = core::Color::rgba(106, 126, 148);
inline const core::Color kReadout = core::Color::rgba(202, 220, 236);
inline const core::Color kBarLow = core::Color::rgba(62, 206, 255);
inline const core::Color kBarHigh = core::Color::rgba(176, 132, 255);
inline const core::Color kScope = core::Color::rgba(122, 255, 198);
inline const core::Color kGlow = core::Color::rgba(60, 220, 172, 56);
inline const core::Color kStepOff = core::Color::rgba(26, 34, 44);
inline const core::Color kStepOn = core::Color::rgba(62, 206, 255);
inline const core::Color kCursor = core::Color::rgba(255, 192, 84);
inline const core::Color kMeter = core::Color::rgba(122, 255, 198);

constexpr float kPad = 8.f;
constexpr float kHeaderHeight = 18.f;
constexpr float kScopeHeight = 96.f;
constexpr float kStepsHeight = 22.f;
constexpr float kFooterHeight = 12.f;
constexpr float kGap = 6.f;
constexpr float kRulerWidth = 30.f;
constexpr float kSmallText = 9.f;

struct Layout {
    core::Rect header;
    core::Rect gutter;
    core::Rect spectrum;
    core::Rect scope;
    core::Rect steps;
    core::Rect footer;
};

inline core::Rect box(float x, float y, float w, float h) {
    return {core::Point{core::X{x}, core::Y{y}},
            core::Size{core::Width{std::max(w, 1.f)}, core::Height{std::max(h, 1.f)}}};
}

inline float left(core::Rect r) { return r.origin.x.raw(); }
inline float top(core::Rect r) { return r.origin.y.raw(); }
inline float right(core::Rect r) { return r.origin.x.raw() + r.extent.w.raw(); }
inline float bottom(core::Rect r) { return r.origin.y.raw() + r.extent.h.raw(); }
inline core::Point at(float x, float y) { return {core::X{x}, core::Y{y}}; }

// Half-pixel offsets: a 1 px line lands on a pixel instead of straddling two.
inline float crisp(float v) { return std::floor(v) + 0.5f; }

inline Layout layout(core::Rect bounds) {
    const float x0 = left(bounds) + kPad;
    const float width = bounds.extent.w.raw() - 2.f * kPad;
    const float y0 = top(bounds) + kPad;
    const float fixed = kHeaderHeight + kScopeHeight + kStepsHeight + kFooterHeight + 4.f * kGap;
    const float spectrum_h = std::max(48.f, bounds.extent.h.raw() - 2.f * kPad - fixed);
    const float spectrum_y = y0 + kHeaderHeight + kGap;
    const float scope_y = spectrum_y + spectrum_h + kGap;
    const float steps_y = scope_y + kScopeHeight + kGap;
    const float footer_y = steps_y + kStepsHeight + kGap;
    return {.header = box(x0, y0, width, kHeaderHeight),
            .gutter = box(x0, spectrum_y, kRulerWidth, spectrum_h),
            .spectrum = box(x0 + kRulerWidth, spectrum_y, width - kRulerWidth, spectrum_h),
            .scope = box(x0 + kRulerWidth, scope_y, width - kRulerWidth, kScopeHeight),
            .steps = box(x0 + kRulerWidth, steps_y, width - kRulerWidth, kStepsHeight),
            .footer = box(x0, footer_y, width, kFooterHeight)};
}

// 0 at the floor, 1 at full scale.
inline float db_fraction(float db) { return std::clamp((db - kFloorDb) / -kFloorDb, 0.f, 1.f); }

// Where a level sits in a plot: full scale at the top, the floor at the bottom. Bars and the
// ruler both go through here, so they cannot disagree about which way the scale runs.
inline float level_y(core::Rect area, float db) {
    return top(area) + (1.f - db_fraction(db)) * area.extent.h.raw();
}

inline float log_hz_fraction(double hz) {
    const double ratio = std::log(hz / SpectrumMap::kLowHz) / std::log(SpectrumMap::kHighHz / SpectrumMap::kLowHz);
    return static_cast<float>(std::clamp(ratio, 0.0, 1.0));
}

inline void draw_mono(render::DrawList& dl, std::string text, core::Point origin, float size,
                      core::Color color) {
    dl.text(std::move(text), origin, size, color, 0.f, render::TextAnchor::TopLeft,
            render::FontRole::Mono);
}

inline void draw_db_ruler(render::DrawList& dl, const Layout& lay) {
    for (const float db : {0.f, -20.f, -40.f, -60.f, -80.f}) {
        const float y = crisp(level_y(lay.spectrum, db));
        dl.line(at(left(lay.spectrum), y), at(right(lay.spectrum), y),
                db == 0.f ? kGridStrong : kGrid);
        // Kept inside the plot's own vertical span: the top label would otherwise sit in the gap
        // above it, against the header line.
        const float label_y = std::clamp(y - 5.f, top(lay.spectrum), bottom(lay.spectrum) - 10.f);
        draw_mono(dl, fmt::format("{}", static_cast<int>(db)), at(left(lay.gutter) + 2.f, label_y),
                  kSmallText, kDim);
    }
}

inline void draw_hz_ruler(render::DrawList& dl, const Layout& lay) {
    for (const double hz : {100.0, 1000.0, 10000.0}) {
        const float x = crisp(left(lay.spectrum) + log_hz_fraction(hz) * lay.spectrum.extent.w.raw());
        dl.line(at(x, top(lay.spectrum)), at(x, bottom(lay.spectrum)), kGrid);
        const std::string label = hz >= 1000.0 ? fmt::format("{}k", static_cast<int>(hz / 1000.0))
                                              : fmt::format("{}", static_cast<int>(hz));
        draw_mono(dl, label, at(x + 3.f, bottom(lay.spectrum) - 12.f), kSmallText, kDim);
    }
}

inline void draw_bars(render::DrawList& dl, const Layout& lay, const AnalyzerFrame& frame) {
    const float slot = lay.spectrum.extent.w.raw() / static_cast<float>(kBandCount);
    const float bar_width = std::max(1.f, slot - 1.f);
    const float baseline = bottom(lay.spectrum) - 1.f;
    for (std::size_t band = 0; band < kBandCount; ++band) {
        if (db_fraction(frame.spectrum_db[band]) <= 0.002f) continue;
        const float bar_top = level_y(lay.spectrum, frame.spectrum_db[band]);
        const float bar_height = std::max(1.f, baseline - bar_top);
        const float x = left(lay.spectrum) + static_cast<float>(band) * slot;
        const auto color = core::lerp(
            kBarLow, kBarHigh,
            core::EasedProgress{static_cast<float>(band) / static_cast<float>(kBandCount - 1)});
        dl.filled_rect(box(x, baseline - bar_height, bar_width, bar_height), color);
    }
}

inline void draw_scope(render::DrawList& dl, const Layout& lay, const AnalyzerFrame& frame,
                       std::vector<core::Point>& scratch) {
    const float centre = crisp(top(lay.scope) + lay.scope.extent.h.raw() * 0.5f);
    dl.line(at(left(lay.scope), centre), at(right(lay.scope), centre), kGrid);
    // Two short lines, so the label stays inside the 30 px gutter: the footer carries the exact
    // peak in dBFS.
    draw_mono(dl, "pk", at(left(lay.gutter) + 2.f, top(lay.scope) + 2.f), kSmallText, kDim);
    draw_mono(dl, fmt::format("{:.0f}", frame.peak_db), at(left(lay.gutter) + 2.f, top(lay.scope) + 13.f),
              kSmallText, kReadout);

    const float gain = lay.scope.extent.h.raw() * 0.45f;
    const float width = lay.scope.extent.w.raw();
    scratch.clear();
    scratch.reserve(kScopeSamples);
    for (std::size_t i = 0; i < kScopeSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kScopeSamples - 1);
        const float sample = std::clamp(frame.scope[i], -1.f, 1.f);
        scratch.push_back(at(left(lay.scope) + width * t, centre - sample * gain));
    }
    dl.polyline(scratch, kGlow, 4.f);
    dl.polyline(std::move(scratch), kScope, 1.4f);
}

inline void draw_steps(render::DrawList& dl, const Layout& lay, const AnalyzerFrame& frame,
                       const SynthSettings& settings) {
    const float slot = lay.steps.extent.w.raw() / static_cast<float>(kSteps);
    for (std::size_t step = 0; step < kSteps; ++step) {
        const core::Rect cell = box(left(lay.steps) + static_cast<float>(step) * slot + 1.f,
                                    top(lay.steps), slot - 2.f, lay.steps.extent.h.raw());
        const bool enabled = step_enabled(settings.step_mask, step);
        const bool playing = settings.powered && step == frame.step;
        dl.filled_rect(cell, enabled ? (playing ? kCursor : kStepOn) : kStepOff);
        if (playing) dl.rect_outline(cell, kCursor, 1.f);
        draw_mono(dl, fmt::format("{}", step + 1),
                  at(left(cell) + 3.f, top(cell) + 3.f), kSmallText,
                  enabled ? kScreen : kDim);
    }
}

inline void draw_header(render::DrawList& dl, const Layout& lay, const SynthSettings& settings) {
    const auto chord = ui::enum_label<Chord>(ui::enum_index(settings.chord));
    const auto pattern = ui::enum_label<ArpPattern>(ui::enum_index(settings.pattern));
    draw_mono(dl,
              fmt::format("{} · {} · {:.0f} BPM · {} oct · {:2} of {} steps on", chord, pattern,
                          settings.tempo_bpm, settings.octaves,
                          std::popcount(static_cast<std::uint32_t>(settings.step_mask)), kSteps),
              at(left(lay.header), top(lay.header)), 10.f, kReadout);
}

inline void draw_footer(render::DrawList& dl, const Layout& lay, const AnalyzerFrame& frame,
                        const SynthSettings& settings) {
    const core::Rect bar = box(left(lay.footer), top(lay.footer) + 2.f, 110.f, 6.f);
    dl.filled_rect(bar, kStepOff);
    const float filled = bar.extent.w.raw() * db_fraction(frame.peak_db);
    if (filled >= 1.f) dl.filled_rect(box(left(bar), top(bar), filled, 6.f), kMeter);

    draw_mono(dl,
              fmt::format("peak {:5.1f} dBFS · {:>3} · {} frame block · {:.1f} kHz · {} waveform",
                          frame.peak_db, note_name(frame.note), frame.buffer_frames,
                          frame.sample_rate / 1000.0,
                          ui::enum_label<Waveform>(ui::enum_index(settings.waveform))),
              at(left(bar) + 118.f, top(lay.footer) - 1.f), kSmallText, kDim);
}

inline void draw_idle(render::DrawList& dl, const Layout& lay) {
    const auto centre = lay.spectrum.center();
    const core::Rect banner = box(centre.x.raw() - 150.f, centre.y.raw() - 11.f, 300.f, 22.f);
    dl.filled_rect(banner, kPanel);
    dl.rect_outline(banner, kGridStrong, 1.f);
    dl.text("POWER OFF — press Power to start the engine", at(left(banner) + 10.f, top(banner) + 5.f),
            10.f, kReadout, 0.f, render::TextAnchor::TopLeft, render::FontRole::Ui);
}

inline void draw(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node,
                 const AnalyzerFrame& frame, const SynthSettings& settings,
                 std::vector<core::Point>& scratch) {
    const Layout lay = layout(bounds);
    dl.filled_rect(bounds, kScreen);
    dl.filled_rect(lay.spectrum, kStrip);
    dl.filled_rect(lay.scope, kStrip);
    draw_db_ruler(dl, lay);
    draw_hz_ruler(dl, lay);
    draw_bars(dl, lay, frame);
    draw_scope(dl, lay, frame, scratch);
    draw_steps(dl, lay, frame, settings);
    draw_header(dl, lay, settings);
    draw_footer(dl, lay, frame, settings);
    if (!settings.powered) draw_idle(dl, lay);
    dl.rounded_rect(bounds, node.theme->border, 0.f, 1.f);
}

} // namespace prism::synth::analyzer_canvas
