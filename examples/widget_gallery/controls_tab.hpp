// Controls: every input widget, each with a tooltip, and a summary line that follows them all.
#pragma once

#include "gallery_icons.hpp"

#include <prism/prism.hpp>
#include <prism/app/window_geometry.hpp>

#include <vector>

namespace gallery {

enum class Quality { Draft, Normal, Best };
enum class Theme { System, Light, Dark };

// What the gallery remembers between runs (app/settings.hpp): saved as settings.toml.
struct Prefs {
    prism::core::Field<Theme> theme{Theme::System};   // a bare enum: a dropdown; applied by main.cpp
    // The Docking tab's panels, as the user arranged them (doc/design/drag-and-docking.md).
    prism::core::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(
        false, {prism::ui::dock_group({"notes", "list"}), prism::ui::dock_group({"controls", "log"})})}};
    prism::app::WindowGeometry window;
};

struct ControlsTab {
    prism::core::Field<prism::ui::Label<>> title{{.value = "Controls", .heading = true}};
    prism::core::Field<prism::ui::TextField<>> name{{.placeholder = "Your name"}};
    prism::core::Field<prism::ui::Password<>> secret{{.placeholder = "Password"}};
    prism::core::Field<prism::ui::Choice> port{{.options = {"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0"},
                                                .placeholder = "Pick a port"}};
    prism::core::Field<prism::ui::ComboBox> fruit{{.value = "", .placeholder = "Pick or type a fruit",
                                     .options = {"Apple", "Apricot", "Banana", "Blueberry", "Cherry", "Mango"}}};
    prism::core::Field<prism::ui::SpinBox<int>> copies{{.value = 2, .min = 1, .max = 20, .step = 1}};
    prism::core::Field<prism::ui::SpinBox<double>> scale{{.value = 1.0, .min = 0.25, .max = 4.0, .step = 0.25}};
    prism::core::Field<prism::ui::Slider<>> volume{{.value = 0.6, .min = 0.0, .max = 1.0, .label = "Volume"}};
    prism::core::Field<prism::ui::Checkbox> notify{{.checked = true, .label = "Send notifications"}};
    prism::core::Field<prism::ui::Radio<Quality>> quality{{Quality::Normal}};
    prism::core::Field<Theme>& theme;   // the gallery's preference, shown here
    prism::core::Field<prism::ui::Button> save{{.text = "Save", .icon = icon("save.svg")}};
    prism::core::Field<prism::ui::Button> reset{{.text = "Reset", .icon = icon("refresh.svg"), .primary = false}};
    prism::core::Field<prism::ui::Button> trash{{.text = "", .icon = icon("trash.svg"), .primary = false}};
    prism::core::Field<prism::ui::ProgressBar> progress{{.value = 0.6, .label = "60%"}};
    prism::core::Field<prism::ui::Checkbox> busy{{.checked = false, .label = "Length unknown (busy)"}};
    prism::core::Field<prism::ui::Label<>> summary{{""}};
    prism::core::Field<prism::ui::Checkbox> lock{{.checked = false, .label = "Lock the settings"}};
    // Flags that grey widgets out (vb.widget(f).enabled, vb.enabled_if); they follow the fields.
    prism::core::Derived<bool> has_name{[this] { return !name.get().value.empty(); }, name};
    prism::core::Derived<bool> unlocked{[this] { return !lock.get().checked; }, lock};
    std::vector<prism::core::Connection> wiring;   // captures `this`: the tab never moves

    explicit ControlsTab(prism::core::Field<Theme>& theme_pref);
    void view(prism::app::WidgetTree::ViewBuilder& vb);   // controls_tab.cpp

private:
    void settings(prism::app::WidgetTree::ViewBuilder& vb);
    void update();
    void clear();
};

} // namespace gallery
