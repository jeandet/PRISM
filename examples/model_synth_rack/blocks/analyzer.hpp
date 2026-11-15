#pragma once

#include <prism/prism.hpp>

#include <vector>

#include "../dsp/settings.hpp"
#include "analyzer_canvas.hpp"

namespace prism::synth::blocks {

// The analyzer panel: the spectrum, the scope, the step playhead and the peak meter, drawn from
// the app's display frame and the settings it was made from. It holds no state of its own except
// the scratch space one draw needs, and it repaints when either input changes.
struct Analyzer {
    // Nested so the model's unplaced-field check, which only looks at this struct's own members,
    // has nothing to say about values a canvas reads: `depends_on` records a Field as placed but
    // not a Derived, and both of these are read here rather than placed as widgets.
    struct Source {
        core::Field<AnalyzerFrame>& frame;
        core::Derived<SynthSettings>& settings;
    };

    explicit Analyzer(core::Field<AnalyzerFrame>& shot, core::Derived<SynthSettings>& live)
        : source{shot, live} {}

    Source source;
    std::vector<core::Point> trace{};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.canvas(*this).depends_on(source.frame, source.settings);
    }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        analyzer_canvas::draw(dl, bounds, node, source.frame.get(), source.settings.get(), trace);
    }
};

} // namespace prism::synth::blocks
