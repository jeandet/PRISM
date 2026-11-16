#include "build_your_own_tab.hpp"

#include <algorithm>
#include <string>

namespace gallery {

void ChannelStrip::view(app::WidgetTree::ViewBuilder& vb) {
    vb.vstack([&] {
        vb.widget(name);
        vb.widget(gain);
        vb.widget(pan);
        vb.widget(mute);
        vb.canvas(*this).depends_on(gain, mute).min_size(core::Height{6.f});
    });
}

void ChannelStrip::canvas(render::DrawList& dl, const core::Rect& bounds, const ui::WidgetNode& node) {
    const auto& t = ui::node_theme(node);
    const bool silent = value(mute);
    const double level = std::clamp((value(gain) + 60.0) / 66.0, 0.0, 1.0);
    const core::Width lit{bounds.extent.w.raw() * static_cast<float>(silent ? 0.0 : level)};
    dl.filled_rect(bounds, t.track);
    dl.filled_rect({bounds.origin, {lit, bounds.extent.h}}, t.accent);
}

BuildYourOwnTab::BuildYourOwnTab() {
    for (std::size_t i = 0; i < strips.size(); ++i)
        strips[i].name.set({.value = std::string{strip_names[i]}});
}

void BuildYourOwnTab::view(Vb& vb) {
    vb.widget(widget_title);
    vb.widget(widget_note);
    vb.gap(14.f);
    vb.hstack([&] {   // the result, centred
        vb.spacer();
        vb.widget(cutoff);
        vb.gap(36.f);
        vb.widget(drive).tooltip("Continuous: no step, so it moves by a hundredth");
        vb.gap(36.f);
        vb.widget(ratio);
        vb.spacer();
    });
    vb.gap(14.f);
    vb.widget(widget_code).tooltip("The whole file, embedded at build time: scroll it");

    vb.gap(28.f);
    vb.widget(strip_title);
    vb.widget(strip_note);
    vb.gap(14.f);
    vb.hstack([&] {
        for (auto& strip : strips) {
            vb.block(strip);
            vb.gap(6.f);
        }
        vb.spacer();
        vb.widget(master);
    });
    vb.gap(14.f);
    vb.widget(strip_code).tooltip("That struct: its view() and canvas() compile in a file of their own");
}

} // namespace gallery
