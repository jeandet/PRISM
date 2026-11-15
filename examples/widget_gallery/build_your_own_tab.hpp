// Build your own: the two things the other tabs cannot show — a widget written from scratch, and
// a block built out of fields. Each section is one sentence, the live result, then its code.
#pragma once

#include "knob.hpp"

#include <prism/prism.hpp>

#include <array>
#include <string>
#include <string_view>

namespace gallery {

// The knob's own source, embedded at compile time, so the page cannot drift from what runs.
// GCC 14 has no #embed; nested #if, because it cannot even parse __has_embed(...).
#if defined(__has_embed)
#if __has_embed("knob.hpp")
#define PRISM_GALLERY_EMBEDS_KNOB 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wc23-extensions"   // clang calls #embed an extension in C++
inline constexpr unsigned char knob_bytes[] = {   // bytes: the source is UTF-8
#embed "knob.hpp"
};
#pragma GCC diagnostic pop
#endif
#endif

inline std::string_view knob_source() {
#ifdef PRISM_GALLERY_EMBEDS_KNOB
    return {reinterpret_cast<const char*>(knob_bytes), sizeof knob_bytes};
#else
    return "// This compiler has no #embed (GCC 15 or clang 19 do): the knob's source is\n"
           "// examples/widget_gallery/knob.hpp.\n";
#endif
}

// A mixer channel: four fields, one view(), and a canvas for the meter. Composition is the whole
// reuse mechanism — no base class, nothing to register.
struct ChannelStrip {
    core::Field<ui::Label<>> name;
    core::Field<Knob> gain{{.min = -60.0, .max = 6.0, .step = 0.5, .label = "Gain", .unit = "dB"}};
    core::Field<Knob> pan{{.min = -1.0, .max = 1.0, .step = 0.05, .label = "Pan"}};
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};

    void view(app::WidgetTree::ViewBuilder& vb);
    // The level meter: the gain as a bar, dead when the channel is muted.
    void canvas(render::DrawList& dl, const core::Rect& bounds, const ui::WidgetNode& node);
};

// The struct above, as one would write it: here its view() and canvas() are compiled separately.
inline constexpr std::string_view strip_source = R"(struct ChannelStrip {
    core::Field<ui::Label<>> name;
    core::Field<Knob> gain{{.min = -60.0, .max = 6.0, .step = 0.5, .label = "Gain", .unit = "dB"}};
    core::Field<Knob> pan{{.min = -1.0, .max = 1.0, .step = 0.05, .label = "Pan"}};
    core::Field<ui::Checkbox> mute{{.label = "Mute"}};

    void view(app::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.widget(name);
            vb.widget(gain);
            vb.widget(pan);
            vb.widget(mute);
            vb.canvas(*this).depends_on(gain, mute).min_size(core::Height{6.f});
        });
    }

    void canvas(render::DrawList& dl, const core::Rect& bounds, const ui::WidgetNode& node) {
        const auto& t = ui::node_theme(node);
        const bool silent = mute.get().checked;
        const double level = std::clamp((gain.get().value + 60.0) / 66.0, 0.0, 1.0);
        const core::Width lit{bounds.extent.w.raw() * static_cast<float>(silent ? 0.0 : level)};
        dl.filled_rect(bounds, t.track);
        dl.filled_rect({bounds.origin, {lit, bounds.extent.h}}, t.accent);
    }
};
)";

struct BuildYourOwnTab {
    using Vb = app::WidgetTree::ViewBuilder;

    static constexpr std::array<std::string_view, 8> strip_names{
        "Drums", "Bass", "Keys", "Pad", "Arp", "Lead", "Vox", "Fx"};
    static constexpr size_t code_rows{18};

    core::Field<ui::Label<>> widget_title{
        {.value = "A widget is one specialization", .heading = true}};
    core::Field<ui::Label<>> widget_note{
        {.value = "Three live knobs from the file below: drag one round, or focus one and "
                  "hold an arrow key.",
         .wrap = true}};
    core::Field<Knob> cutoff{{.value = 2400.0, .min = 100.0, .max = 12000.0, .step = 10.0,
                              .label = "Cutoff", .unit = "Hz"}};
    core::Field<Knob> drive{{.value = 0.35, .min = 0.0, .max = 1.0, .step = 0.01, .label = "Drive"}};
    core::Field<Knob> ratio{{.value = 4.0, .min = 1.0, .max = 20.0, .step = 0.5,
                             .label = "Ratio", .unit = ":1"}};
    core::Field<ui::TextArea<>> widget_code{{.value = std::string{knob_source()},
                                             .rows = code_rows,
                                             .read_only = true}};

    core::Field<ui::Label<>> strip_title{{.value = "A block is a struct", .heading = true}};
    core::Field<ui::Label<>> strip_note{
        {.value = "Four fields, a view() and a canvas: eight strips side by side are a mixer, and "
                  "the ninth knob is the master.",
         .wrap = true}};
    std::array<ChannelStrip, 8> strips{};
    core::Field<Knob> master{{.value = -3.0, .min = -60.0, .max = 6.0, .step = 0.5,
                              .label = "Master", .unit = "dB"}};
    core::Field<ui::TextArea<>> strip_code{{.value = std::string{strip_source},
                                            .rows = code_rows,
                                            .read_only = true}};

    BuildYourOwnTab();
    void view(Vb& vb);   // build_your_own_tab.cpp
};

} // namespace gallery
