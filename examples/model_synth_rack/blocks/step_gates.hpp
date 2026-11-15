#pragma once

#include <prism/prism.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <vector>

#include "../dsp/arpeggiator.hpp"

namespace prism::synth::blocks {

// The step gates: one checkbox per 16th note of the pattern, and the mask they make. Editing a
// box is what fills the mask, so the mask follows from sixteen bools with no other bookkeeping.
struct StepGates {
    // Nested so the app model's unplaced-field check has nothing to say about a mask that is
    // not a widget.
    struct Port {
        core::Field<std::uint16_t> mask{0xFFFF};
    };

    std::array<core::Field<ui::Checkbox>, kSteps> boxes{};
    Port port{};

    StepGates() {
        for (std::size_t step = 0; step < kSteps; ++step) {
            boxes[step].set({.checked = true, .label = fmt::format("{}", step + 1)});
            reactions_.push_back(
                boxes[step].on_change().connect([this, step](const ui::Checkbox& box) {
                    set_gate(step, box.checked);
                }));
        }
    }

    std::uint16_t mask() const { return port.mask.get(); }

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.hstack([&] {
            for (std::size_t step = 0; step < kSteps; ++step)
                vb.widget(boxes[step]).tooltip(fmt::format("Gate for step {}", step + 1));
        });
    }

private:
    void set_gate(std::size_t step, bool open) {
        const auto bit = static_cast<std::uint16_t>(1u << step);
        const std::uint16_t mask = port.mask.get();
        port.mask.set(open ? static_cast<std::uint16_t>(mask | bit)
                           : static_cast<std::uint16_t>(mask & static_cast<std::uint16_t>(~bit)));
    }

    // Declared last, so the connections are torn down before the fields they observe.
    std::vector<core::Connection> reactions_{};
};

} // namespace prism::synth::blocks
