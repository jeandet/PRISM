#include <embedded/audio_scope.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prism_examples {
namespace {

using prism::plot::AxisRange;

// A plot series over a buffer the scope owns and rewrites in place: PRISM's pattern for live
// data (perf_lab's RingPlotSource) — the series is added once, each update bumps the revision.
struct BufferSource {
    const std::vector<float> *ys;
    double x_step;
    std::size_t size() const { return ys->size(); }
    double x(std::size_t i) const { return double(i) * x_step; }
    double y(std::size_t i) const { return (*ys)[i]; }
};

AxisRange fixed(double min, double max) { return {.min = min, .max = max, .auto_fit = false}; }

AxisRange spectrum_y_range(bool db) { return db ? fixed(Spectrum::kFloorDb, 0.0) : fixed(0.0, 1.0); }

// Shifts `incoming` into the end of `window`, dropping its oldest samples.
void shift_in(std::vector<float> &window, std::span<const float> incoming)
{
    if (incoming.size() >= window.size()) {
        std::ranges::copy(incoming.last(window.size()), window.begin());
        return;
    }
    std::shift_left(window.begin(), window.end(), std::ptrdiff_t(incoming.size()));
    std::ranges::copy(incoming, window.end() - std::ptrdiff_t(incoming.size()));
}

} // namespace

void ScopeModel::view(prism::app::WidgetTree::ViewBuilder &vb)
{
    vb.vstack([&] {
        vb.widget(title);
        vb.hstack([&] {
            vb.widget(frozen);
            vb.gap(12.f);
            vb.widget(db_scale);
            vb.gap(12.f);
            vb.widget(fit);
            vb.gap(12.f);
            vb.widget(gain);
        });
        vb.hstack([&] {
            for (auto *plot : {&waveform, &spectrum})
                vb.canvas(*plot)
                    .min_size(prism::core::Height{130.f})
                    .depends_on(plot->x_range)
                    .depends_on(plot->y_range)
                    .depends_on(plot->view)
                    .depends_on(plot->cursor)
                    .depends_on(plot->revision);
        });
        vb.widget(stats);
    });
}

void scope_bind(ScopeModel &model, ScopeState &state)
{
    using S = ScopeState;
    const double rate = state.source_.sample_rate();
    model.title.set({.value = std::string("PRISM · ") + state.source_.name(), .heading = true});
    model.waveform.add_series(BufferSource{&state.waveform_, 1000.0 / rate}, {});
    model.waveform.x_range.set(fixed(0.0, double(S::kWaveformSamples) * 1000.0 / rate));
    model.waveform.y_range.set(fixed(-1.0, 1.0));
    model.waveform.x_label.set("ms");
    model.spectrum.add_series(BufferSource{&state.spectrum_, rate / double(S::kFftSize)}, {});
    model.spectrum.x_range.set(fixed(0.0, rate / 2.0));
    model.spectrum.x_label.set("Hz");
    model.spectrum.y_range.set(spectrum_y_range(true));
}

bool ScopeState::drain_source(bool frozen)
{
    bool any = false;
    while (const auto n = source_.read(scratch_)) {
        samples_since_report_ += n;
        for (float v : std::span<const float>(scratch_).first(n)) {
            peak_since_report_ = std::max(peak_since_report_, std::abs(v));
            sum_since_report_ += v;
        }
        if (!frozen)
            shift_in(window_, std::span<const float>(scratch_).first(n));
        any = true;
    }
    return any && !frozen;
}

void scope_tick(ScopeModel &model, ScopeState &state)
{
    const bool fresh = state.drain_source(model.frozen.get().checked);
    const double gain = model.gain.get().value;
    const bool db = model.db_scale.get().checked;
    if (db != state.shown_db_)
        model.spectrum.y_range.set(spectrum_y_range(db));
    if (fresh || gain != state.shown_gain_ || db != state.shown_db_)
        state.update_plots(model);
    if (const auto clicks = model.fit.get().click_count; clicks != state.fit_clicks_) {
        state.fit_clicks_ = clicks;
        state.fit_ranges(model);
    }
}

// Like an oscilloscope's autoset: once, on the samples shown now. The waveform gets its peak
// with some headroom; the spectrum its peak at the top and 60 dB below it (dB), or 0 .. peak.
void ScopeState::fit_ranges(ScopeModel &model)
{
    constexpr double kHeadroom = 1.2;
    float peak = 0.f;
    for (float v : waveform_)
        peak = std::max(peak, std::abs(v));
    const double half = std::max(double(peak) * kHeadroom, 1e-4);
    model.waveform.y_range.set(fixed(-half, half));
    const double top = *std::ranges::max_element(spectrum_);
    model.spectrum.y_range.set(shown_db_ ? fixed(top - 60.0, top + 6.0)
                                         : fixed(0.0, std::max(top * kHeadroom, 1e-6)));
}

void ScopeState::update_plots(ScopeModel &model)
{
    const auto gain = float(model.gain.get().value);
    const bool db = model.db_scale.get().checked;
    std::ranges::transform(window_, gained_.begin(), [gain](float v) { return v * gain; });
    std::ranges::transform(std::span<const float>(gained_).last(kWaveformSamples), waveform_.begin(),
                           [](float v) { return std::clamp(v, -1.f, 1.f); });
    if (db)
        fft_.compute(gained_, spectrum_);
    else
        fft_.compute_linear(gained_, spectrum_);
    model.waveform.notify();
    model.spectrum.notify();
    shown_gain_ = gain;
    shown_db_ = db;
}

void scope_show_stats(ScopeModel &model, ScopeState &state, std::string_view line)
{
    // In place: the label's string keeps its memory, so a new line allocates nothing.
    model.stats.mutate([line](prism::ui::Label<> &label) { label.value.assign(line); });
    state.show_level(model);
}

// "PRISM · A0 · 16000 S/s · dc +0.01 · peak -42 dBFS": what arrived since the last report. The
// DC level tells a stuck input (a data line held high reads +1) from a quiet one.
void ScopeState::show_level(ScopeModel &model)
{
    const char *name = source_.name();
    int n = 0;
    if (samples_since_report_ == 0)
        n = std::snprintf(title_text_.data(), title_text_.size(), "PRISM · %s · no samples", name);
    else if (peak_since_report_ == 0.f)
        n = std::snprintf(title_text_.data(), title_text_.size(), "PRISM · %s · %zu S/s · silent",
                          name, samples_since_report_);
    else
    {
        // + 0.0 turns a rounded -0.00 into 0.00.
        const double dc = std::round(sum_since_report_ / double(samples_since_report_) * 100.0) / 100.0 + 0.0;
        n = std::snprintf(title_text_.data(), title_text_.size(),
                          "PRISM · %s · %zu S/s · dc %+.2f · peak %.0f dBFS", name, samples_since_report_, dc,
                          20.0 * std::log10(double(peak_since_report_)));
    }
    const std::string_view text(title_text_.data(), std::size_t(std::clamp(n, 0, int(title_text_.size()) - 1)));
    model.title.mutate([text](prism::ui::Label<> &label) { label.value.assign(text); });
    samples_since_report_ = 0;
    peak_since_report_ = 0.f;
    sum_since_report_ = 0.0;
}

void attach_scope(prism::platform::FrameContext &ctx, ScopeModel &model, ScopeState &state)
{
    scope_bind(model, state);
    ctx.on_loop([&model, &state] { scope_tick(model, state); });
    ctx.on_stats([&model, &state](std::string_view line) { scope_show_stats(model, state, line); });
}

} // namespace prism_examples
