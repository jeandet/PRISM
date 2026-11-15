#pragma once

#include <prism/app/settings.hpp>
#include <prism/prism.hpp>
#include <prism/ui/dock.hpp>

// The bench's docked layout and window size, persisted like any other setting. At namespace scope:
// the settings file is reflected over a type with external linkage.
struct LabBenchPrefs {
    prism::core::Field<prism::ui::DockLayout> layout{{prism::ui::dock_split(
        /*vertical=*/false,
        {prism::ui::dock_split(/*vertical=*/true,
                               {prism::ui::dock_group({"scope"}),
                                prism::ui::dock_group({"event_log", "diagnostics"})},
                               {0.72f, 0.28f}),
         prism::ui::dock_split(/*vertical=*/true,
                               {prism::ui::dock_group({"power_supply"}),
                                prism::ui::dock_group({"wave_gen"})},
                               {0.52f, 0.48f})},
        {0.60f, 0.40f})}};
    prism::app::WindowGeometry window;
};
