#pragma once

#include <prism/app/window.hpp>
#include <prism/core/field.hpp>

// A window's size, place and maximized state, kept in the app's settings (app/settings.hpp) so
// it reopens as it was left: AppContext::remember_window_size keeps it current, applied_to()
// uses it at the next start.
namespace prism::app {

struct WindowGeometry {
    core::Field<int> width{0};    // 0: nothing remembered yet
    core::Field<int> height{0};
    core::Field<int> x{0};
    core::Field<int> y{0};
    core::Field<bool> positioned{false};   // x and y remembered (never on Wayland)
    core::Field<bool> maximized{false};

    // `config` with what is remembered.
    [[nodiscard]] WindowConfig applied_to(WindowConfig config) const {
        if (width.get() > 0 && height.get() > 0) {
            config.width = width.get();
            config.height = height.get();
        }
        if (positioned.get()) {
            config.x = x.get();
            config.y = y.get();
        }
        config.maximized = maximized.get();
        return config;
    }
};

} // namespace prism::app
