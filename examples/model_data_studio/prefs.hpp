#pragma once

#include <prism/app/settings.hpp>
#include <prism/prism.hpp>
#include <prism/ui/dock.hpp>

// The studio's docked layout and window size, persisted like any other setting. At namespace scope:
// the settings file is reflected over a type with external linkage.
struct DataStudioPrefs {
    prism::core::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(
        /*vertical=*/false,
        {prism::ui::dock_split(/*vertical=*/true,
                               {prism::ui::dock_group({"schema"}), prism::ui::dock_group({"queries"})},
                               {0.46f, 0.54f}),
         prism::ui::dock_split(/*vertical=*/true,
                               {prism::ui::dock_group({"editor"}), prism::ui::dock_group({"results"})},
                               {0.46f, 0.54f})},
        {0.24f, 0.76f})}};
    prism::app::WindowGeometry window;
};
