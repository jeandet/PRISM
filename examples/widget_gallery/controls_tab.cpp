#include "controls_tab.hpp"

#include <fmt/format.h>

#include <string>

namespace gallery {

ControlsTab::ControlsTab(prism::core::Field<Theme>& theme_pref) : theme(theme_pref) {
    wire.on_any([this] { update(); }, name, fruit, copies, scale, notify, quality, theme);
    wire.on(volume, [this](const prism::ui::Slider<>& v) {
        progress.set({.value = v.value, .label = fmt::format("{:.0f}%", v.value * 100), .busy = busy.get().checked});
        update();
    });
    wire.on(busy, [this](const prism::ui::Checkbox& b) {
        auto p = progress.get();
        p.busy = b.checked;
        progress.set(p);
    });
    wire.on(reset, [this](const auto&) { clear(); });
    wire.on(trash, [this](const auto&) { name.set({.placeholder = "Your name"}); });
    update();
}

void ControlsTab::view(prism::app::WidgetTree::ViewBuilder& vb) {
    vb.widget(title);
    vb.hstack([&] {
        vb.widget(name).tooltip("A text field: selection, clipboard, undo, input methods");
        vb.gap(8.f);
        vb.widget(secret).tooltip("A password field: shows dots, never copies");
    });
    vb.gap(6.f);
    vb.widget(fruit).tooltip("A combo box: type freely, or pick a suggestion with the arrows");
    vb.widget(port).tooltip("A choice among options known at run time, nothing else");
    vb.gap(6.f);
    vb.widget(lock).tooltip("Greys out the settings below, down to the checkboxes");
    vb.gap(6.f);
    vb.enabled_if(unlocked, [&] { settings(vb); });
    vb.gap(6.f);
    vb.hstack([&] {
        vb.widget(save).enabled(has_name).tooltip("Enabled once the name is filled in");
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

void ControlsTab::settings(prism::app::WidgetTree::ViewBuilder& vb) {
    vb.hstack([&] {
        vb.widget(copies).tooltip("Whole numbers: arrows, the wheel, or type a value");
        vb.gap(8.f);
        vb.widget(scale).tooltip("Steps of 0.25");
        vb.gap(8.f);
        vb.widget(theme).tooltip("An enum field is a dropdown. This one switches the theme, and is remembered");
    });
    vb.gap(6.f);
    vb.widget(volume).tooltip("Drag, click, or use the arrow keys");
    vb.gap(6.f);
    vb.hstack([&] {
        vb.widget(notify).tooltip("A checkbox");
        vb.gap(16.f);
        vb.group("Quality", [&] { vb.widget(quality).tooltip("An enum shown as radio buttons"); });
    });
}

void ControlsTab::update() {
    summary.set({fmt::format("{} · {} · {} × {:.2f} · {} · notifications {}",
                             name.get().value.empty() ? "nobody" : name.get().value,
                             fruit.get().value.empty() ? "no fruit" : fruit.get().value,
                             copies.get().value, scale.get().value,
                             quality.get().value == Quality::Draft  ? "draft"
                             : quality.get().value == Quality::Best ? "best" : "normal",
                             notify.get().checked ? "on" : "off")});
}

void ControlsTab::clear() {
    name.set({.placeholder = "Your name"});
    fruit.set({.placeholder = "Pick or type a fruit", .options = fruit.get().options});
    copies.set({.value = 2, .min = 1, .max = 20, .step = 1});
    volume.set({.value = 0.6, .min = 0.0, .max = 1.0});
    notify.set({.checked = true, .label = "Send notifications"});
}

} // namespace gallery
