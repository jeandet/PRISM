#pragma once

#include <prism/prism.hpp>

#include <vector>

#include "../daq/model.hpp"
#include "scope_canvas.hpp"

namespace prism::lab::blocks {

// The scope panel: the instrument's screen, drawn from the last frame the model received. It holds
// nothing but the scratch space one draw needs, and repaints when a frame arrives -- and only then.
struct Scope {
    explicit Scope(core::Field<DaqTelemetry>& shot) : frame{shot} {}

    core::Field<DaqTelemetry>& frame;
    std::vector<core::Point> trace{};

    void view(app::WidgetTree::ViewBuilder& vb) { vb.canvas(*this).depends_on(frame); }

    void canvas(render::DrawList& dl, core::Rect bounds, const ui::WidgetNode& node) {
        scope_canvas::draw(dl, bounds, node, frame.get(), trace);
    }
};

} // namespace prism::lab::blocks
