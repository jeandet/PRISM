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
    core::Field<AnalyzerFrame>& frame;        // the app's: it owns them, this panel draws them
    core::Derived<SynthSettings>& settings;
    std::vector<core::Point> trace{};

    void view(app::ViewBuilder& vb) { vb.canvas(*this).depends_on(frame, settings); }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        analyzer_canvas::draw(dl, bounds, node, frame.get(), settings.get(), trace);
    }
};

} // namespace prism::synth::blocks
