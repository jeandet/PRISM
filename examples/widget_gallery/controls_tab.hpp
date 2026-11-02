// Controls: every input widget, each with a tooltip, and a summary line that follows them all.
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>

#include <fmt/format.h>

#include <string>
#include <vector>

namespace gallery {

enum class Quality { Draft, Normal, Best };
enum class Theme { System, Light, Dark };

struct ControlsTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Controls", .heading = true}};
    prism::core::Field<prism::ui::TextField<>> name{{.placeholder = "Your name"}};
    prism::core::Field<prism::ui::Password<>> secret{{.placeholder = "Password"}};
    prism::core::Field<prism::ui::ComboBox> fruit{{.value = "", .placeholder = "Pick or type a fruit",
                                     .options = {"Apple", "Apricot", "Banana", "Blueberry", "Cherry", "Mango"}}};
    prism::core::Field<prism::ui::SpinBox<int>> copies{{.value = 2, .min = 1, .max = 20, .step = 1}};
    prism::core::Field<prism::ui::SpinBox<double>> scale{{.value = 1.0, .min = 0.25, .max = 4.0, .step = 0.25}};
    prism::core::Field<prism::ui::Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 1.0}};
    prism::core::Field<prism::ui::Checkbox> notify{{.checked = true, .label = "Send notifications"}};
    prism::core::Field<prism::ui::Radio<Quality>> quality{{Quality::Normal}};
    prism::core::Field<Theme> theme{Theme::System};   // a bare enum: a dropdown
    prism::core::Field<prism::ui::Button> save{{.text = "Save", .icon = icon("save.svg")}};
    prism::core::Field<prism::ui::Button> reset{{.text = "Reset", .icon = icon("refresh.svg"), .primary = false}};
    prism::core::Field<prism::ui::Button> trash{{.text = "", .icon = icon("trash.svg"), .primary = false}};
    prism::core::Field<prism::ui::ProgressBar> progress{{.value = 0.6, .label = "60%"}};
    prism::core::Field<prism::ui::Checkbox> busy{{.checked = false, .label = "Length unknown (busy)"}};
    prism::core::Field<prism::ui::Label<>> summary{{""}};
    std::vector<prism::core::Connection> wiring;   // captures `this`: the tab never moves

    ControlsTab() {
        auto refresh = [this](const auto&) { update(); };
        wiring.push_back(name.on_change().connect(refresh));
        wiring.push_back(fruit.on_change().connect(refresh));
        wiring.push_back(copies.on_change().connect(refresh));
        wiring.push_back(scale.on_change().connect(refresh));
        wiring.push_back(notify.on_change().connect(refresh));
        wiring.push_back(quality.on_change().connect(refresh));
        wiring.push_back(theme.on_change().connect(refresh));
        wiring.push_back(volume.on_change().connect([this](const prism::ui::Slider<>& v) {
            progress.set({.value = v.value, .label = fmt::format("{:.0f}%", v.value * 100), .busy = busy.get().checked});
            update();
        }));
        wiring.push_back(busy.on_change().connect([this](const prism::ui::Checkbox& b) {
            auto p = progress.get();
            p.busy = b.checked;
            progress.set(p);
        }));
        wiring.push_back(reset.on_change().connect([this](const auto&) { clear(); }));
        wiring.push_back(trash.on_change().connect([this](const auto&) { name.set({.placeholder = "Your name"}); }));
        update();
    }

    void view(prism::app::WidgetTree::ViewBuilder& vb) {
        vb.widget(title);
        vb.hstack([&] {
            vb.widget(name).tooltip("A text field: selection, clipboard, undo, input methods");
            vb.gap(8.f);
            vb.widget(secret).tooltip("A password field: shows dots, never copies");
        });
        vb.gap(6.f);
        vb.widget(fruit).tooltip("A combo box: type freely, or pick a suggestion with the arrows");
        vb.gap(6.f);
        vb.hstack([&] {
            vb.widget(copies).tooltip("Whole numbers: arrows, the wheel, or type a value");
            vb.gap(8.f);
            vb.widget(scale).tooltip("Steps of 0.25");
            vb.gap(8.f);
            vb.widget(theme).tooltip("An enum field is a dropdown");
        });
        vb.gap(6.f);
        vb.widget(volume).tooltip("Drag, click, or use the arrow keys");
        vb.gap(6.f);
        vb.hstack([&] {
            vb.widget(notify).tooltip("A checkbox");
            vb.gap(16.f);
            vb.widget(quality).tooltip("An enum shown as radio buttons");
        });
        vb.gap(6.f);
        vb.hstack([&] {
            vb.widget(save).tooltip("A primary button with an icon");
            vb.gap(8.f);
            vb.widget(reset).tooltip("A quieter button: clears the form");
            vb.gap(8.f);
            vb.widget(trash).tooltip("An icon-only button needs a tooltip as its name");
            vb.spacer();
        });
        vb.gap(6.f);
        vb.widget(progress).tooltip("Follows the slider");
        vb.widget(busy).tooltip("For work whose length is unknown");
        vb.gap(6.f);
        vb.widget(summary);
    }

private:
    void update() {
        summary.set({fmt::format("{} · {} · {} × {:.2f} · {} · notifications {}",
                                 name.get().value.empty() ? "nobody" : name.get().value,
                                 fruit.get().value.empty() ? "no fruit" : fruit.get().value,
                                 copies.get().value, scale.get().value,
                                 quality.get().value == Quality::Draft  ? "draft"
                                 : quality.get().value == Quality::Best ? "best" : "normal",
                                 notify.get().checked ? "on" : "off")});
    }

    void clear() {
        name.set({.placeholder = "Your name"});
        fruit.set({.placeholder = "Pick or type a fruit", .options = fruit.get().options});
        copies.set({.value = 2, .min = 1, .max = 20, .step = 1});
        volume.set({.value = 0.6, .min = 0.0, .max = 1.0});
        notify.set({.checked = true, .label = "Send notifications"});
    }
};

} // namespace gallery
