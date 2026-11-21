#pragma once

// The widget demo: a title, a counter that "+1" increments, a checkbox, a slider, and the
// runtime's timing line under them. A hand-written view() -- the board's toolchain has no
// reflection. Widgets are the delegates' default size (30 px tall, slider 30 px with a caption)
// plus explicit gaps, which on each board's panel is as finger-sized as the layout gets.

#include <prism/app/widget_tree.hpp>
#include <prism/core/connection.hpp>
#include <prism/core/field.hpp>
#include <prism/platform/frame_app.hpp>

#include <string_view>

namespace prism_examples {

struct WidgetsModel {
    prism::core::Field<prism::ui::Label<>> title{{.value = "PRISM", .heading = true}};
    prism::core::Field<int> counter{0};
    prism::core::Field<prism::ui::Button> increment{{.text = "+1"}};
    prism::core::Field<prism::ui::Checkbox> enabled{{.checked = true, .label = "enabled"}};
    prism::core::Field<prism::ui::Slider<>> volume{
        {.value = 0.5, .min = 0.0, .max = 1.0, .step = 0.05, .label = "volume"}};
    prism::core::Field<prism::ui::Label<>> stats{{.value = "measuring…", .wrap = true}};
    prism::core::Connection on_increment = increment.on_change().connect(
        [this](const prism::ui::Button &) { counter.set(counter.get() + 1); });

    WidgetsModel() = default;
    // The connection's callback holds `this`: a copy or move would count on the old model.
    WidgetsModel(const WidgetsModel &) = delete;
    WidgetsModel &operator=(const WidgetsModel &) = delete;
    WidgetsModel(WidgetsModel &&) = delete;
    WidgetsModel &operator=(WidgetsModel &&) = delete;

    void view(prism::app::WidgetTree::ViewBuilder &vb)
    {
        vb.vstack([&] {
            vb.widget(title);
            vb.gap(12.f);
            vb.widget(counter);
            vb.gap(12.f);
            vb.hstack([&] {
                vb.widget(increment);
                vb.gap(16.f);
                vb.widget(enabled);
            });
            vb.gap(12.f);
            vb.widget(volume);
            vb.widget(stats);
        });
    }
};

// The widget demo has no loop work: only the timing line, into its label.
inline void attach_widgets(prism::platform::FrameContext &ctx, WidgetsModel &model)
{
    ctx.on_stats([&model](std::string_view line) {
        // In place: the label's string keeps its memory, so a new line allocates nothing.
        model.stats.mutate([line](prism::ui::Label<> &label) { label.value.assign(line); });
    });
}

} // namespace prism_examples
