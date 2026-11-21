#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <embedded/audio_scope.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/raster/fonts.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace {
constexpr float kRate = 16000.f;
constexpr float kBinHz = kRate / 1024.f;

// Hands out exactly the samples the test queued.
struct QueuedSource final : prism_examples::SampleSource {
    std::deque<float> queued;
    std::size_t read(std::span<float> out) override
    {
        const auto n = std::min(out.size(), queued.size());
        std::copy_n(queued.begin(), n, out.begin());
        queued.erase(queued.begin(), queued.begin() + std::ptrdiff_t(n));
        return n;
    }
    float sample_rate() const override { return kRate; }
    const char *name() const override { return "queued"; }

    void push_sine(float freq, std::size_t n, float amplitude = 0.5f)
    {
        for (std::size_t i = 0; i < n; ++i)
            queued.push_back(amplitude *
                             std::sin(2.f * std::numbers::pi_v<float> * freq * float(i) / kRate));
    }
};

float peak_hz(std::span<const float> spectrum)
{
    return float(std::distance(spectrum.begin(), std::ranges::max_element(spectrum))) * kBinHz;
}

// The scope as an app holds it: the model, the state beside it, the plots bound to the state.
struct Scope {
    prism_examples::ScopeState state; // first: the model's plots read its buffers
    prism_examples::ScopeModel model;
    explicit Scope(prism_examples::SampleSource &source) : state(source) { prism_examples::scope_bind(model, state); }
    void tick() { prism_examples::scope_tick(model, state); }
    void show_stats(std::string_view line) { prism_examples::scope_show_stats(model, state, line); }
    std::span<const float> waveform() const { return state.waveform(); }
    std::span<const float> spectrum() const { return state.spectrum(); }
};

void set_gain(Scope &scope, double g)
{
    auto slider = scope.model.gain.get();
    slider.value = g;
    scope.model.gain.set(slider);
}

void set_frozen(Scope &scope, bool frozen)
{
    auto box = scope.model.frozen.get();
    box.checked = frozen;
    scope.model.frozen.set(box);
}
} // namespace

TEST_CASE("audio scope: a 1 kHz block gives a 320-point waveform and a spectrum peak at 1 kHz")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 1024);
    scope.tick();
    CHECK(scope.waveform().size() == 320);
    CHECK(scope.spectrum().size() == 512);
    CHECK(scope.model.waveform.series_len(0) == 320);
    CHECK(scope.model.spectrum.series_len(0) == 512);
    CHECK(std::abs(peak_hz(scope.spectrum()) - 1000.f) <= kBinHz);
}

TEST_CASE("audio scope: fewer samples than one FFT at startup still gives finite plots")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 100);
    scope.tick();
    CHECK(std::ranges::all_of(scope.waveform(), [](float v) { return std::isfinite(v); }));
    CHECK(std::ranges::all_of(scope.spectrum(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("audio scope: maximum gain on a full-scale sine clamps the waveform, stays finite")
{
    QueuedSource src;
    Scope scope{src};
    set_gain(scope, 8.0);
    src.push_sine(1000.f, 1024, 1.f);
    scope.tick();
    CHECK(std::ranges::all_of(scope.waveform(), [](float v) { return v >= -1.f && v <= 1.f; }));
    CHECK(std::ranges::all_of(scope.spectrum(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("audio scope: samples that arrive while frozen are dropped")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 1024);
    scope.tick();
    const std::vector<float> before(scope.spectrum().begin(), scope.spectrum().end());

    set_frozen(scope, true);
    src.push_sine(2000.f, 1024);
    scope.tick();
    CHECK(std::ranges::equal(scope.spectrum(), before));

    set_frozen(scope, false);
    src.push_sine(3000.f, 512);
    scope.tick();
    const auto bin = [](float hz) { return std::size_t(hz / kBinHz); };
    CHECK(scope.spectrum()[bin(3000.f)] > -20.f);  // the new samples are there
    CHECK(scope.spectrum()[bin(2000.f)] < -40.f);  // the frozen ones are not
}

TEST_CASE("audio scope: the view builds a snapshot and shows the stats line")
{
    QueuedSource src;
    Scope scope{src};
    prism::app::WidgetTree tree{scope.model};
    prism::raster::Fonts fonts;
    tree.set_text_measurer(&fonts);
    src.push_sine(1000.f, 1024);
    scope.tick();
    scope.show_stats("12 fps");
    CHECK(scope.model.stats.get().value == "12 fps");
    auto snap = tree.build_snapshot(480, 272, 1);
    REQUIRE(snap != nullptr);
    CHECK_FALSE(snap->draw_lists.empty());
    CHECK(scope.model.title.get().value.starts_with("PRISM · queued"));
}

namespace {
void press_fit(Scope &scope)
{
    auto button = scope.model.fit.get();
    ++button.click_count;
    scope.model.fit.set(button);
}
} // namespace

TEST_CASE("audio scope: fit scales the waveform to the signal's peak")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 1024, 0.01f); // a quiet input: invisible on the default ±1
    scope.tick();
    press_fit(scope);
    scope.tick();
    const auto y = scope.model.waveform.y_range.get();
    CHECK_FALSE(y.auto_fit);
    CHECK(y.max == doctest::Approx(0.012).epsilon(0.05));
    CHECK(y.min == doctest::Approx(-y.max));
}

TEST_CASE("audio scope: fit frames the spectrum's peak, and new samples keep that range")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 1024, 0.01f);
    scope.tick();
    press_fit(scope);
    scope.tick();
    const auto fitted = scope.model.spectrum.y_range.get();
    const float peak = *std::ranges::max_element(scope.spectrum());
    CHECK(fitted.max > peak);
    CHECK(fitted.max < -20.0);          // well below the default top of 0 dB
    CHECK(fitted.min < peak - 40.0);    // the noise floor stays in view
    src.push_sine(1000.f, 512, 0.01f);
    scope.tick();
    CHECK(scope.model.spectrum.y_range.get() == fitted);
}

TEST_CASE("audio scope: switching dB or linear goes back to that scale's full range")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 1024, 0.01f);
    scope.tick();
    press_fit(scope);
    scope.tick();
    auto box = scope.model.db_scale.get();
    box.checked = false;
    scope.model.db_scale.set(box);
    scope.tick();
    CHECK(scope.model.spectrum.y_range.get().max == doctest::Approx(1.0));
}

TEST_CASE("audio scope: the title reports the samples received and their peak each second")
{
    QueuedSource src;
    Scope scope{src};
    src.push_sine(1000.f, 16000, 0.5f);
    for (int i = 0; i < 20; ++i)
        scope.tick();
    scope.show_stats("1 fps");
    CHECK(scope.model.title.get().value == "PRISM · queued · 16000 S/s · dc +0.00 · peak -6 dBFS");
    scope.show_stats("1 fps"); // a second with no samples at all
    CHECK(scope.model.title.get().value == "PRISM · queued · no samples");
}

TEST_CASE("audio scope: the title shows a stuck input's DC level")
{
    QueuedSource src;
    Scope scope{src};
    src.queued.assign(16000, 1.f); // a data line held high reads full scale, constantly
    for (int i = 0; i < 20; ++i)
        scope.tick();
    scope.show_stats("1 fps");
    CHECK(scope.model.title.get().value == "PRISM · queued · 16000 S/s · dc +1.00 · peak 0 dBFS");
}
