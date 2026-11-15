#pragma once

#include <prism/app/settings.hpp>
#include <prism/prism.hpp>
#include <prism/ui/dock.hpp>

// Docked panel layout and window size, persisted like any other setting. At namespace scope: the
// settings file is reflected over a type with external linkage.
struct RackSettings {
    prism::core::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(
        /*vertical=*/true,
        {prism::ui::dock_split(/*vertical=*/false,
                               {prism::ui::dock_group({"analyzer"}),
                                prism::ui::dock_split(/*vertical=*/true,
                                                      {prism::ui::dock_group({"arp"}),
                                                       prism::ui::dock_group({"voice"}),
                                                       prism::ui::dock_group({"engine"})},
                                                      {0.31f, 0.41f, 0.28f})},
                               {0.68f, 0.32f}),
         prism::ui::dock_group({"steps"})},
        {0.85f, 0.15f})}};
    prism::app::WindowGeometry window;
};
