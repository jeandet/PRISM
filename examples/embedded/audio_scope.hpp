#pragma once

// The audio scope: a SampleSource (a board's input (an ADC, microphones), or a generated sweep)
// shown as a waveform plot (the last 20 ms) and a spectrum plot (0 .. half the sample rate), with
// freeze, gain, dB/linear and fit controls and the runtime's timing line. The title reports the
// samples received each second and their peak level, so a silent or stalled source shows.
// Everything goes through PRISM (plot::PlotModel on vb.canvas), so what is slow on the board is
// PRISM's cost. A model (ScopeModel), the state beside it (ScopeState), and the functions the
// runtime calls on the two (attach_scope).

#include <embedded/sample_source.hpp>
#include <embedded/spectrum.hpp>

#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/platform/frame_app.hpp>
#include <prism/widgets/plot.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prism_examples {

struct ScopeModel {
    prism::core::Field<prism::ui::Label<>> title{{.value = "PRISM scope", .heading = true}};
    prism::core::Field<prism::ui::Checkbox> frozen{{.checked = false, .label = "freeze"}};
    prism::core::Field<prism::ui::Checkbox> db_scale{{.checked = true, .label = "dB"}};
    prism::core::Field<prism::ui::Slider<>> gain{
        {.value = 1.0, .min = 0.5, .max = 8.0, .step = 0.5, .label = "gain"}};
    prism::core::Field<prism::ui::Button> fit{{.text = "fit"}}; // y ranges to the signal now
    prism::plot::PlotModel waveform, spectrum;
    prism::core::Field<prism::ui::Label<>> stats{{.value = "measuring…", .wrap = true}};

    void view(prism::app::WidgetTree::ViewBuilder &vb);
};

// What the scope holds besides its model: the sample window, the FFT, the plotted buffers and
// the title's level readout. The model's plots draw from this state's buffers (scope_bind), so
// it must outlive the model's use.
class ScopeState {
public:
    explicit ScopeState(SampleSource &source) : source_(source) {}

    std::span<const float> waveform() const { return waveform_; }  // what the waveform plot shows
    std::span<const float> spectrum() const { return spectrum_; }  // what the spectrum plot shows

    static constexpr std::size_t kFftSize = 1024;
    static constexpr std::size_t kWaveformSamples = 320; // 20 ms at 16 kHz

private:
    friend void scope_bind(ScopeModel &, ScopeState &);
    friend void scope_tick(ScopeModel &, ScopeState &);
    friend void scope_show_stats(ScopeModel &, ScopeState &, std::string_view);

    bool drain_source(bool frozen); // true when new samples entered the window
    void update_plots(ScopeModel &model);
    void fit_ranges(ScopeModel &model);
    void show_level(ScopeModel &model);

    SampleSource &source_;
    Spectrum fft_{kFftSize};
    std::vector<float> window_ = std::vector<float>(kFftSize, 0.f); // the last kFftSize samples
    std::vector<float> scratch_ = std::vector<float>(kFftSize);
    std::vector<float> gained_ = std::vector<float>(kFftSize);
    std::vector<float> waveform_ = std::vector<float>(kWaveformSamples, 0.f);
    std::vector<float> spectrum_ = std::vector<float>(kFftSize / 2, Spectrum::kFloorDb);
    double shown_gain_ = 0.0; // the settings the plots were last computed with
    bool shown_db_ = false;
    std::uint64_t fit_clicks_ = 0;
    std::size_t samples_since_report_ = 0; // for the title's level readout
    float peak_since_report_ = 0.f;
    double sum_since_report_ = 0.0;
    std::array<char, 96> title_text_{};
};

// Points the model's plots at the state's buffers and titles it after the source. Once, before
// the first tick.
void scope_bind(ScopeModel &model, ScopeState &state);
// Each logic loop: drains the source, recomputes the plots when the samples or settings changed,
// fits the ranges when "fit" was clicked.
void scope_tick(ScopeModel &model, ScopeState &state);
// Once a second: the timing line into the stats label, the level readout into the title.
void scope_show_stats(ScopeModel &model, ScopeState &state, std::string_view line);
// Binds the scope and registers the two above as the runtime's on_loop and on_stats.
void attach_scope(prism::platform::FrameContext &ctx, ScopeModel &model, ScopeState &state);

} // namespace prism_examples
